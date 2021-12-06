#include "gposkern.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>  // memcmp

static inline unsigned short get_USHORT(const unsigned char *buf) // {{{
{
  return (buf[0]<<8)|(buf[1]);
}
// }}}

static inline short get_SHORT(const unsigned char *buf) // {{{
{
  return ((signed char)buf[0]<<8)|(buf[1]);
}
// }}}

static inline unsigned int get_ULONG(const unsigned char *buf) // {{{
{
  return (buf[0]<<24)|
         (buf[1]<<16)|
         (buf[2]<<8)|
         (buf[3]);
}
// }}}


// NOTE: expects len >= 6
// returns (maxFeatureIndex + 1) or -1
static int check_langsys(const unsigned char *buf, size_t len) // {{{
{
  const unsigned short lookupOrderOffset = get_USHORT(buf),
                       requiredFeatureIndex = get_USHORT(buf + 2),
                       featureIndexCount = get_USHORT(buf + 4);
  if (featureIndexCount * 2 > len - 6) {
    return -1;
  }
  if (lookupOrderOffset != 0) {
//    fprintf(stderr, "Error: lookupOrderOffset cannot be != 0\n");  // TODO?
    return -1;
  }

  int ret = (requiredFeatureIndex != 0xffff) ? requiredFeatureIndex : 0;
  buf += 6;
  for (int i = 0; i < featureIndexCount; i++, buf += 2) {
    const unsigned short featureIndex = get_USHORT(buf);
    if (featureIndex > ret) {
      ret = featureIndex;
    }
  }

  return ret;
}
// }}}

// NOTE: expects len >= 2
// returns (maxFeatureIndex + 1) or -1
static int check_scriptlist(const unsigned char *buf, size_t len) // {{{
{
  const unsigned short scriptCount = get_USHORT(buf);
  if (scriptCount * 6 > len - 2) {
    return -1;
  }

  // NOTE: DFLT is not required...
  int ret = 0;
  for (int i = 0; i < scriptCount; i++) {
    const unsigned char *tag = buf + 2 + 6 * i;
    const unsigned short scriptOffset = get_USHORT(tag + 4);
    if (scriptOffset > len - 4) {
      return -1;
    }

    // Script table
    const unsigned char *tmp = buf + scriptOffset;
    const size_t len1 = len - scriptOffset;
    const unsigned short defaultLangSysOffset = get_USHORT(tmp),
                         langSysCount = get_USHORT(tmp + 2);
    if (langSysCount * 6 > len1 - 4) {
      return -1;
    }

    if (memcmp(tag, "DFLT", 4) == 0) {
      if (!defaultLangSysOffset) { // tag 'DFLT' MUST have defaultLangSysOffset
        return -1;
      }
      if (i != 0) {
        fprintf(stderr, "Warning: Expected DFLT script to be first\n");  // the registered scripts start with lowercase letter ...
      }
    }

    if (defaultLangSysOffset) {
      if (defaultLangSysOffset > len1 - 6) {
        return -1;
      }

      const int res = check_langsys(tmp + defaultLangSysOffset, len1 - defaultLangSysOffset);
      if (res < 0) {
        return -1;
      } else if (res > ret) {
        ret = res;
      }
    }

    // LangSysRecord
    for (int j = 0; j < langSysCount; j++) {
      const unsigned char *tag = tmp + 4 + 6 * j;
      const unsigned short langSysOffset = get_USHORT(tag + 4);
      if (langSysOffset > len - scriptOffset - 6) {
        return -1;
      }

      const int res = check_langsys(tmp + langSysOffset, len1 - langSysOffset);
      if (res < 0) {
        return -1;
      } else if (res > ret) {
        ret = res;
      }
    }
  }

  return ret;
}
// }}}

  // TODO? bsearch_tag ?
// buf shall point to start of GPOS
// returns pointer to LangSys table or NULL
static const unsigned char *lookup_script(const unsigned char *buf, const char *script, const char *language, int fallbackDFLT) // {{{
{
  // assert(buf);
  if (!script) {
    if (!fallbackDFLT) {
      return NULL;
    }
    script = "DFLT";
  }

  const unsigned short scriptListOffset = get_USHORT(buf + 4);
  buf += scriptListOffset;

  int scriptOffset = -1;
// TODO? bsearch_tag ?
  const unsigned short scriptCount = get_USHORT(buf);
  for (int i = 0; i < scriptCount; i++) {
    const unsigned char *tag = buf + 2 + 6 * i;
    if (memcmp(tag, script, 4) == 0) {
      scriptOffset = get_USHORT(tag + 4);
      break;
    }
  }

  if (scriptOffset < 0) {
    if (fallbackDFLT &&
        scriptCount > 0 &&
        memcmp(buf + 2, "DFLT", 4) == 0) { // DFLT would be first entry, only check that...
      scriptOffset = get_USHORT(buf + 2 + 4);
    } else {
      return NULL;
    }
  }

  const unsigned char *tmp = buf + scriptOffset;
  const unsigned short defaultLangSysOffset = get_USHORT(tmp),
                       langSysCount = get_USHORT(tmp + 2);
  if (language) {
// TODO? bsearch_tag ?
    for (int i = 0; i < langSysCount; i++) {
      const unsigned char *tag = tmp + 4 + 6 * i;
      if (memcmp(tag, language, 4) == 0) {
        const unsigned short langSysOffset = get_USHORT(tag + 4);
        return tmp + langSysOffset;
      }
    }
  }
  if (defaultLangSysOffset) {
    return tmp + defaultLangSysOffset;
  }

  return NULL;
}
// }}}


// NOTE: expects len >= 2
// returns (maxLookupIndex + 1) or -1
static int check_featurelist(const unsigned char *buf, size_t len) // {{{
{
  const unsigned short featureCount = get_USHORT(buf);
  if (featureCount * 6 > len - 2) {
    return -1;
  }

  int ret = 0;
  for (int i = 0; i < featureCount; i++) {
    const unsigned short featureOffset = get_USHORT(buf + 2 + 6 * i + 4);
    if (featureOffset > len - 4) {
      return -1;
    }

    const unsigned char *tmp = buf + featureOffset;
    const size_t len1 = len - featureOffset;
    const unsigned short featureParamsOffset = get_USHORT(tmp),
                         lookupIndexCount = get_USHORT(tmp + 2);
    if (lookupIndexCount * 2 > len1 - 4) {
      return -1;
    }
    if (featureParamsOffset) {
      if (featureParamsOffset > len1) {
        return -1;
      }
      // NOTE: cannot easily check size of FeatureParams header (or even full FeatureParams), because it depends on the featureTag
    }

    // Feature table
    tmp += 4;
    for (int j = 0; j < lookupIndexCount; j++, tmp += 2) {
      const unsigned short lookupIndex = get_USHORT(tmp);
      if (ret < lookupIndex + 1) {
        ret = lookupIndex + 1;
      }
    }
  }

  return ret;
}
// }}}

static void _set_usedLookups_of_featuretable(const unsigned char *buf, unsigned char *usedLookups) // {{{
{
  const unsigned short lookupIndexCount = get_USHORT(buf + 2);
  buf += 4;
  for (int i = 0; i < lookupIndexCount; i++, buf += 2) {
    const unsigned short lookupIndex = get_USHORT(buf);
    usedLookups[lookupIndex] = 1;
  }
}
// }}}

static void _set_usedLookups_of_feature(const unsigned char *featurelist, unsigned short featureIndex, unsigned char *usedLookups, int (*filterFn)(const unsigned char *tag, void *user), void *user) // {{{
{
  const unsigned char *tag = featurelist + 2 + 6 * featureIndex;
  if (filterFn(tag, user)) {
    const unsigned short featureOffset = get_USHORT(tag + 4);
    _set_usedLookups_of_featuretable(featurelist + featureOffset, usedLookups);
  }
}
// }}}

static void set_usedLookups_of_features(const unsigned char *featurelist, const unsigned char *langsys, unsigned char *usedLookups, int (*filterFn)(const unsigned char *tag, void *user), void *user) // {{{
{
  const unsigned short requiredFeatureIndex = get_USHORT(langsys + 2),
                       featureIndexCount = get_USHORT(langsys + 4);
  if (requiredFeatureIndex != 0xffff) {
    // assert(requiredFeatureIndex < featureCount);
    _set_usedLookups_of_feature(featurelist, requiredFeatureIndex, usedLookups, filterFn, user);
  }
  for (int i = 0; i < featureIndexCount; i++) {
    const unsigned short featureIndex = get_USHORT(langsys + 6 + 2 * i);
    // assert(featureIndex < featureCount);
    _set_usedLookups_of_feature(featurelist, featureIndex, usedLookups, filterFn, user);
  }
}
// }}}


// returns glyphCount or -1
static int check_coverage(const unsigned char *buf, size_t len) // {{{
{
  if (len < 4) {
    return -1;
  }

  const unsigned short coverageFormat = get_USHORT(buf);
  buf += 2;
  if (coverageFormat == 1) {
    const unsigned short glyphCount = get_USHORT(buf);
    if (glyphCount * 2 > len - 4) {
      return -1;
    }
    return glyphCount;

  } else if (coverageFormat == 2) {
    const unsigned short rangeCount = get_USHORT(buf);
    if (rangeCount * 6 > len - 4) {
      return -1;
    }

    unsigned short glyphCount = 0;
    int prevGlyphID = -1;
    for (int i = 0; i < rangeCount; i++) {
      const unsigned short startGlyphID = get_USHORT(buf += 2),
                           endGlyphID = get_USHORT(buf += 2),
                           startCoverageIndex = get_USHORT(buf += 2);
      if (startGlyphID <= prevGlyphID ||
          endGlyphID < startGlyphID ||
          startCoverageIndex != glyphCount) {
        return -1;
      }
      glyphCount += endGlyphID - startGlyphID + 1;
      prevGlyphID = endGlyphID;
    }
    return glyphCount;
  }

  return -1;
}
// }}}

// returns index or -1
static int lookup_coverage(const unsigned char *buf, unsigned short gid) // {{{
{
  const unsigned short coverageFormat = get_USHORT(buf);
  buf += 2;
  if (coverageFormat == 1) {
    const unsigned short glyphCount = get_USHORT(buf);
// TODO? bsearch ?
    for (int i = 0; i < glyphCount; i++) {
      const unsigned short glyphID = get_USHORT(buf += 2);
      if (glyphID == gid) {
        return i;
      }
    }

  } else if (coverageFormat == 2) {
    const unsigned short rangeCount = get_USHORT(buf);
// TODO? lbound ?
    for (int i = 0; i < rangeCount; i++) {
      const unsigned short startGlyphID = get_USHORT(buf += 2);
      if (startGlyphID > gid) {
        break;
      }
      const unsigned short endGlyphID = get_USHORT(buf += 2);
      buf += 2;
      if (gid <= endGlyphID) {
        const unsigned short startCoverageIndex = get_USHORT(buf);
        return startCoverageIndex + gid - startGlyphID;
      }
    }
  }

  return -1;
}
// }}}

// NOTE: expects len >= 2
// returns maxClass or -1
static int check_classdef(const unsigned char *buf, size_t len) // {{{
{
  const unsigned short classFormat = get_USHORT(buf);
  buf += 2;
  if (classFormat == 1) {
    if (len < 6) {
      return -1;
    }

    buf += 2;
    const unsigned short glyphCount = get_USHORT(buf);
    if (glyphCount * 2 > len - 6) {
      return -1;
    }
    buf += 2;

    unsigned short maxClass = 0;
    for (int i = 0; i < glyphCount; i++, buf += 2) {
      const unsigned short classValue = get_USHORT(buf);
      if (classValue > maxClass) {
        maxClass = classValue;
      }
    }

    return maxClass;

  } else if (classFormat == 2) {
    if (len < 4) {
      return -1;
    }

    const unsigned short classRangeCount = get_USHORT(buf);
    if (classRangeCount * 6 > len - 4) {
      return -1;
    }
    buf += 2;

    unsigned short maxClass = 0;
    for (int i = 0; i < classRangeCount; i++, buf += 6) {
      const unsigned short classValue = get_USHORT(buf + 4);
      if (classValue > maxClass) {
        maxClass = classValue;
      }
    }

    return maxClass;
  }

  return -1;
}
// }}}

// NOTE: "Any glyph not covered by a ClassRangeRecord is assumed to belong to Class 0."
static unsigned short lookup_class(const unsigned char *buf, unsigned short gid) // {{{
{
  const unsigned short classFormat = get_USHORT(buf);
  buf += 2;
  if (classFormat == 1) {
    const unsigned short startGlyphID = get_USHORT(buf);
    if (gid < startGlyphID) {
      return 0;
    }
    buf += 2;
    gid -= startGlyphID;
    const unsigned short glyphCount = get_USHORT(buf);
    if (gid >= glyphCount) {
      return 0;
    }
    buf += 2;
    const unsigned short classValue = get_USHORT(buf + 2 * gid);
    return classValue;

  } else if (classFormat == 2) {
// TODO? lbound ?
    const unsigned short classRangeCount = get_USHORT(buf);
    for (int i = 0; i < classRangeCount; i++) {
      const unsigned short startGlyphID = get_USHORT(buf += 2);
      if (startGlyphID > gid) {
        break;
      }
      const unsigned short endGlyphID = get_USHORT(buf += 2);
      buf += 2;
      if (gid <= endGlyphID) {
        const unsigned short classValue = get_USHORT(buf);
        return classValue;
      }
    }
  }

  return 0;
}
// }}}

enum {
  VF_X_PLACEMENT        = 0x0001,
  VF_Y_PLACEMENT        = 0x0002,
  VF_X_ADVANCE          = 0x0004,
  VF_Y_ADVANCE          = 0x0008,
  VF_X_PLACEMENT_DEVICE = 0x0010,
  VF_Y_PLACEMENT_DEVICE = 0x0020,
  VF_X_ADVANCE_DEVICE   = 0x0040,
  VF_Y_ADVANCE_DEVICE   = 0x0080,
  VF_RESERVED           = 0xFF00
};

// TODO? use __builtin_popcount() ?  // (clang; but: older gcc has non-optimal impl)
static int _valueFormat_size(unsigned short valueFormat) // {{{ or: -1 on error
{
  if (valueFormat & VF_RESERVED) { // reserved
    return -1;
  }
  // popcount ...
  unsigned int bits = valueFormat;
  bits -= ((bits >> 1) & 0x55);
  bits = (bits & 0x33) + ((bits >> 2) & 0x33);
  bits = (bits + (bits >> 4)) & 0x0f;
  return bits * 2;
}
// }}}

// NOTE: expects len >= 2
static int check_lookup2_subtable(const unsigned char *buf, size_t len) // {{{
{
  // PairPos
  const unsigned short posFormat = get_USHORT(buf);
  if (posFormat == 1) {
    if (len < 10) {
      return 0;
    }
    const unsigned short coverageOffset = get_USHORT(buf + 2),
                         valueFormat1 = get_USHORT(buf + 4),
                         valueFormat2 = get_USHORT(buf + 6),
                         pairSetCount = get_USHORT(buf + 8);
    const int valueSize1 = _valueFormat_size(valueFormat1),
              valueSize2 = _valueFormat_size(valueFormat2);
    if (valueSize1 < 0 || valueSize2 < 0) {
      return 0;
    }
    const unsigned short pairValueRecordSize = 2 + valueSize1 + valueSize2;
    if (pairSetCount * 2 > len - 10) {
      return 0;
    }

    const int coverageGlyphCount = check_coverage(buf + coverageOffset, len - coverageOffset);
    if (coverageGlyphCount < 0) {   //  || valueCount != coverageGlyphCount) {
      return 0;
    }
#if 1
    else if (coverageGlyphCount > pairSetCount) {
      fprintf(stderr, "Error: GPOS lookup type 2 with pairSetCount (%d) < coverage.glyphCount (%d) is not supported\n",
              pairSetCount, coverageGlyphCount);
      return 0;
    }
#endif

    // PairSet tables
    const unsigned char *tmp = buf + 10;
    for (int i = 0; i < pairSetCount; i++, tmp += 2) {
      const unsigned short pairSetOffset = get_USHORT(buf);
      if (pairSetOffset > len - 2) {
        return 0;
      }
      const unsigned short pairValueCount = get_USHORT(buf + pairSetOffset);
      if (pairValueCount * pairValueRecordSize > len - pairSetOffset - 2) {
        return 0;
      }
    }
    return 1;

  } else if (posFormat == 2) {
    if (len < 16) {
      return 0;
    }
    const unsigned short coverageOffset = get_USHORT(buf + 2),
                         valueFormat1 = get_USHORT(buf + 4),
                         valueFormat2 = get_USHORT(buf + 6),
                         classDef1Offset = get_USHORT(buf + 8),
                         classDef2Offset = get_USHORT(buf + 10),
                         class1Count = get_USHORT(buf + 12),
                         class2Count = get_USHORT(buf + 14);
    if (classDef1Offset > len - 2 ||
        classDef2Offset > len - 2) {
      return 0;
    }
    const int valueSize1 = _valueFormat_size(valueFormat1),
              valueSize2 = _valueFormat_size(valueFormat2);
    if (valueSize1 < 0 || valueSize2 < 0) {
      return 0;
    }
    const unsigned short class2RecordSize = valueSize1 + valueSize2;
    const unsigned short class1RecordSize = class2Count * class2RecordSize;
    if (class1Count * class1RecordSize > len - 16) {
      return 0;
    }

    if (check_coverage(buf + coverageOffset, len - coverageOffset) < 0) {
      return 0;
    }

    const int maxClass1 = check_classdef(buf + classDef1Offset, len - classDef1Offset),
              maxClass2 = check_classdef(buf + classDef2Offset, len - classDef2Offset);
    if (maxClass1 < 0 || maxClass2 < 0 ||
        maxClass1 + 1 > class1Count ||  // (esp.: class1Count > 0 !)
        maxClass2 + 1 > class2Count) {
      return 0;
    }

    return 1;
  }

  return 0;
}
// }}}

enum {
  LF_RIGHT_TO_LEFT             = 0x0001,
  LF_IGNORE_BASE_GLYPHS        = 0x0002,
  LF_IGNORE_LIGATURES          = 0x0004,
  LF_IGNORE_MARKS              = 0x0008,
  LF_USE_MARK_FILTERING_SET    = 0x0010,
  LF_RESERVED                  = 0x00E0,
  LF_MARK_ATTACHMENT_TYPE_MASK = 0xFF00
};

// NOTE: expects len >= 2
// returns 0 on success, or -1
static int check_lookuplist(const unsigned char *buf, size_t len) // {{{
{
  const unsigned short lookupCount = get_USHORT(buf);
  if (lookupCount * 2 > len - 2) {
    return -1;
  }
  for (int i = 0; i < lookupCount; i++) {
    const unsigned short lookupOffset = get_USHORT(buf + 2 + 2 * i);
    if (lookupOffset > len - 6) {
      return -1;
    }

    // Lookup table
    const unsigned char *tmp = buf + lookupOffset;
    const size_t len1 = len - lookupOffset;
    const unsigned short lookupType = get_USHORT(tmp),
                         lookupFlag = get_USHORT(tmp + 2),
                         subTableCount = get_USHORT(tmp + 4);
    if (lookupFlag & LF_RESERVED) {
      return -1;
    } else if (subTableCount * 2 > len1 - 6 - (lookupFlag & LF_USE_MARK_FILTERING_SET ? 2 : 0)) {
      return -1;
    }

    if (lookupType == 9) {
      fprintf(stderr, "Warning: lookupType 9 (Extension) not yet supported - ignored!\n");
      continue;
      // TODO: lookupType = extension.lookupType; ... 32bit offset redirect ...
    }

    if ((lookupFlag & LF_RIGHT_TO_LEFT) && lookupType != 3) {
      return -1;
    }

    // SubTable
    for (int j = 0; j < subTableCount; j++) {
      const unsigned short subTableOffset = get_USHORT(tmp + 6 + 2 * j);
      if (subTableOffset > len1 - 2) { // ensures len >=2 for check_lookup*_subtable()
        return -1;
      }

      const unsigned char *tmp2 = tmp + subTableOffset;
      const size_t len2 = len1 - subTableOffset;
      switch (lookupType) {
      case 2: // PairPos
        if (!check_lookup2_subtable(tmp2, len2)) {
          return -1;
        }
        break;

      default:
        break;  // just ignore (i.e. do not check) other tables we don't use ...
//        return -1;   // TODO?
      }
    }
  }

  return 0;
}
// }}}


static int check_GPOS(const unsigned char *buf, size_t len) // {{{
{
  if (!buf || len < 10) {
    return 0;
  }

  const unsigned short majorVersion = get_USHORT(buf),
                       minorVersion = get_USHORT(buf + 2);
  if (majorVersion != 1 || minorVersion > 1) {
    fprintf(stderr, "Warning: GPOS version %d.%d not supported\n", majorVersion, minorVersion);
    return 0;
  }

  const unsigned short scriptListOffset = get_USHORT(buf + 4),
                       featureListOffset = get_USHORT(buf + 6),
                       lookupListOffset = get_USHORT(buf + 8);
  unsigned short featureVariationsOffset = 0;
  if (scriptListOffset > len - 2 ||
      featureListOffset > len - 2 ||
      lookupListOffset > len - 2) {
    return 0;
  }
  if (minorVersion == 1) {
    if (len < 14) {
      return 0;
    }
    featureVariationsOffset = get_ULONG(buf + 10);
    if (featureVariationsOffset > len - 8) {
      return 0;
    }
  }

//  const unsigned short scriptCount = get_USHORT(buf + scriptListOffset);

  const int maxFeatureIndex_1 = check_scriptlist(buf + scriptListOffset, len - scriptListOffset);
  const unsigned short featureCount = get_USHORT(buf + featureListOffset);
  if (maxFeatureIndex_1 < 0 || maxFeatureIndex_1 > featureCount) {
    return 0;
  }

  const int max_LookupIndex_1 = check_featurelist(buf + featureListOffset, len - featureListOffset);
  const unsigned short lookupCount = get_USHORT(buf + lookupListOffset);
  if (max_LookupIndex_1 < 0 || max_LookupIndex_1 > lookupCount) {
    return 0;
  }

  if (check_lookuplist(buf + lookupListOffset, len - lookupListOffset) < 0) {
    return 0;
  }

  // ignore featureVariations, for now.

  return 1;
}
// }}}


static int is_lookup_simplekern(const unsigned char *buf) // {{{
{
  const unsigned short lookupType = get_USHORT(buf),
                       lookupFlag = get_USHORT(buf + 2),
                       subTableCount = get_USHORT(buf + 4);

  // assert((lookupFlag & LF_RESERVED) == 0); // via check_lookuplist()
  if (lookupFlag != 0) {
    fprintf(stderr, "Warning: lookupFlag != 0 will not be honored\n");
  }

  if (lookupType == 9) {
    // (TODO: lookupType = extension.lookupType; ... 32bit offset redirect ... )
    // -> warning already printed by check_lookuplist()
    return 0; // ignore
  }

  if (lookupType != 2) {
    return 0;
  }

  unsigned char active = 0xff;
  for (int i = 0; i < subTableCount; i++) {
    const unsigned short subTableOffset = get_USHORT(buf + 6 + 2 * i);
    const unsigned char *tmp = buf + subTableOffset;
    // const unsigned short posFormat = get_USHORT(tmp);
    // assert(posFormat == 1 || posFormat == 2); // via check_lookup2_subtable()
    const unsigned short valueFormat1 = get_USHORT(tmp + 4), // same for posFormat 1/2
                         valueFormat2 = get_USHORT(tmp + 6);

    if ((active & 0x01) && valueFormat2 > 0) { // would mean that for 'WaY', when 'Wa' is kerned, kerning lookup for 'aY' is skipped
      fprintf(stderr, "Warning: Ignoring kern feature with valueFormat2 != 0\n");
      active &= ~0x01;
    }

    // assert((valueFormat1 & VF_RESERVED) == 0); // via _valueFormat_size() < 0
    if ((active & 0x02) && (valueFormat1 & (VF_X_PLACEMENT | VF_Y_PLACEMENT | VF_Y_ADVANCE))) {
      // (NOTE: possibly all values are 0... - but why would a font generator do this?)  // (TODO?? check all valueRecords ?)
      fprintf(stderr, "Warning: valueFormat & (X_PLACEMENT | Y_PLACEMENT | Y_ADVANCE) will not be honored\n");
      active &= ~0x02;
    }
  }

  return 1;
}
// }}}

static int _get_ValueRecord_xAdvance(const unsigned char *buf, unsigned short valueFormat) // {{{
{
  buf += 2 * ((valueFormat & VF_X_PLACEMENT) + ((valueFormat & VF_Y_PLACEMENT) >> 1)); // 0x0001, 0x0002
  return (valueFormat & VF_X_ADVANCE) ? get_SHORT(buf) : 0; // 0x0004
}
// }}}

// buf must point to Lookup table with lookupType == 2, which must be is_lookup2_subtable_simple()
static int get_lookuptable2(const unsigned char *buf, unsigned short firstGID, unsigned short secondGID) // {{{
{
  int ret = 0;

// TODO: special case for (lookupType == 9)  [i.e. ExtensionPos / 32bit offset redirect -> lookupType = extensionLookupType; ]
  // assert(get_USHORT(buf) == 2);  // lookupType
  // assert(get_USHORT(buf + 2) == 0);  // lookupFlag
  const unsigned short subTableCount = get_USHORT(buf + 4);
  for (int i = 0; i < subTableCount; i++) {
    const unsigned short subTableOffset = get_USHORT(buf + 6 + 2 * i);
    const unsigned char *tmp = buf + subTableOffset;
    const unsigned short posFormat = get_USHORT(tmp);
    // assert(posFormat == 1 || posFormat == 2); // via check_lookup2_subtable()
    const unsigned short coverageOffset = get_USHORT(tmp + 2), // same for posFormat 1/2
                         valueFormat1 = get_USHORT(tmp + 4),
                         valueFormat2 = get_USHORT(tmp + 6);
    if (valueFormat2 > 0 ||   // (warning in is_lookup_simplekern())
        (valueFormat1 & VF_X_ADVANCE) == 0) {
      continue;
    }

    const int coverageIndex = lookup_coverage(tmp + coverageOffset, firstGID);
    if (coverageIndex < 0) {
      continue;
    }

    if (posFormat == 1) {
      const unsigned short pairSetCount = get_USHORT(tmp + 8);
      if (coverageIndex >= pairSetCount) {
        continue;
      }

      const unsigned short valueSize1 = _valueFormat_size(valueFormat1);
      const unsigned short pairValueRecordSize = 2 + valueSize1 + 0; // + valueSize2;

      const unsigned short pairSetOffset = get_USHORT(tmp + 10 + 2 * coverageIndex);
// FIXME: lbound/besarch
      const unsigned short pairValueCount = get_USHORT(tmp + pairSetOffset);
      const unsigned char *tmp2 = tmp + pairSetOffset + 2;
      for (int j = 0; j < pairValueCount; j++, tmp2 += pairValueRecordSize) {
        const unsigned short secondGlyph = get_USHORT(tmp2);
        if (secondGlyph > secondGID) {
          break;
        } else if (secondGlyph == secondGID) {
          ret += _get_ValueRecord_xAdvance(tmp2 + 2, valueFormat1);
          // ... _get_ValueRecord_xAdvance(tmp2 + 2 + valueSize1, valueFormat2);
          break;
        }
      }

    } else if (posFormat == 2) {
      const unsigned short classDef1Offset = get_USHORT(tmp + 8),
                           classDef2Offset = get_USHORT(tmp + 10),
                           class2Count = get_USHORT(tmp + 14);

      const unsigned short valueSize1 = _valueFormat_size(valueFormat1);
      const unsigned short class2RecordSize = valueSize1 + 0;  // + valueSize2;
      const unsigned short class1RecordSize = class2Count * class2RecordSize;

      const unsigned short class1 = lookup_class(tmp + classDef1Offset, firstGID),
                           class2 = lookup_class(tmp + classDef2Offset, secondGID);
      // assert(class1 < get_USHORT(tmp + 12)); // class1Count
      // assert(class2 < class2Count);

      const unsigned char *tmp2 = tmp + 16 + class1 * class1RecordSize + class2 * class2RecordSize;
      ret += _get_ValueRecord_xAdvance(tmp2, valueFormat1);
      // ... _get_ValueRecord_xAdvance(tmp2 + valueSize1, valueFormat2);
    }
  }

  return ret;
}
// }}}

struct _gpos_pair_lookup_t {
  // only type2 tables!
  size_t num_lookups;
  const unsigned char *base[]; // pointers to Lookup tables (cf. get_lookuptable2())
};

static int _tagfilter_kern(const unsigned char *tag, void *user) // {{{
{
  (void)user; // unused
  return (memcmp(tag, "kern", 4) == 0);
}
// }}}

// script / language can be NULL
// returns NULL when not parseable/valid/supported
gpos_pair_lookup_t *gpos_pair_lookup_create(const unsigned char *buf, size_t len, const char *script, const char *language)
{
  if (!check_GPOS(buf, len)) {
    return NULL;
  }

  const unsigned char *langsys = lookup_script(buf, script, language, 1);
  if (!langsys) {
    return (gpos_pair_lookup_t *)calloc(1, sizeof(gpos_pair_lookup_t));  // i.e. empty: (num_lookups == 0)
  }

  const unsigned short lookupListOffset = get_USHORT(buf + 8);
  const unsigned short lookupCount = get_USHORT(buf + lookupListOffset);
  unsigned char *usedLookups = (unsigned char *)calloc(lookupCount, sizeof(unsigned char));
  if (!usedLookups) {
    return NULL;
  }

  const unsigned short featureListOffset = get_USHORT(buf + 6);
  set_usedLookups_of_features(buf + featureListOffset, langsys, usedLookups, _tagfilter_kern, NULL);

  int num_lookups = 0;
  for (int i = 0; i < lookupCount; i++) {
    if (usedLookups[i]) {
      const unsigned short lookupOffset = get_USHORT(buf + lookupListOffset + 2 + 2 * i);
      if (!is_lookup_simplekern(buf + lookupListOffset + lookupOffset)) {
        usedLookups[i] = 0;
        continue;
      }
      num_lookups++;
    }
  }

//  gpos_pair_lookup_t *ret = (gpos_pair_lookup_t *)malloc(sizeof(gpos_pair_lookup_t) + num_lookups * sizeof(const unsigned char *));
  gpos_pair_lookup_t *ret = (gpos_pair_lookup_t *)malloc(sizeof(gpos_pair_lookup_t) + num_lookups * sizeof(((gpos_pair_lookup_t *)0)->base[0]));
  if (!ret) {
    free(usedLookups);
    return NULL;
  }
  ret->num_lookups = num_lookups;

  for (int i = 0, pos = 0; i < lookupCount; i++) {
    if (usedLookups[i]) {
      const unsigned short lookupOffset = get_USHORT(buf + lookupListOffset + 2 + 2 * i);
      ret->base[pos++] = buf + lookupListOffset + lookupOffset;
    }
  }

  free (usedLookups);
  return ret;
}

int gpos_pair_lookup_get(const gpos_pair_lookup_t *gpl, unsigned short firstGID, unsigned short secondGID)
{
  // assert(gpl);
  int ret = 0;
  for (size_t i = 0; i < gpl->num_lookups; i++) {
    ret += get_lookuptable2(gpl->base[i], firstGID, secondGID);
  }
  return ret;
}

void gpos_pair_lookup_destroy(gpos_pair_lookup_t *gpl)
{
  free(gpl);
}

