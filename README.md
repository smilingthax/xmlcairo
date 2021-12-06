# Expose cairo graphics api via xml

* Maps rather directly to cairo's api, compared to SVG.
* Not completely low-level, e.g. `<set-source image="img0" x="30" width="20" gravity="nw"/>`
  already does the necessary fit-into-box calculations.

* Supports SVG Path + SVG Transform strings.
* Font/Text with kerning (not just toy api; but also not harfbuzz/pango, yet),  
  with support for automatic downscaling (`<text font="font1" size="20" max-width="100">A very long test text.</text>`).

* Multiple backends (pdf, ps, png, svg, script).
* Unified IO via libxml2 xmlio functions (except freetype / font files [TODO?]).

TODO:
* fonts and images must currently be loaded beforehand...
* use root-level `<surface>` attributes as surface-factory parameters...

Not yet implemented:
* push_group / pop_group -> `<group content="color">...</group>`
* General patterns (gradients, mesh); pattern filter, extend, matrix
* `<set ctm="1 0 0 1 2 3"/>` (or, possibly: `<set ctm="matrix(...)"/>`), "reset ctm"  
  (but there is `<sub transform="matrix(1 0 0 1 2 3)">...</sub>`)
* Dash pattern / offset(phase)
* cairo_tag_begin ... ?

Ideas:
* libxslt extension
* Utilize libgdk-pixbuf to support more image formats
* text: tracking (aka. global kerning)
* Helpers for rounded rectangle, ellipse, polygon, ... ?
* named paths ?
* `<fit width="..." height="...">...</fit>` ?
* emscripten / wasm

Example:
```
<?xml version="1.0" encoding="utf-8"?>
<surface type="png" width="100" height="100">
  <set-source image="tex0" width="100"/>
  <paint alpha="0.4"/>

  <sub transform="scale(0.5)">
    <set-source r="1.0" g="1.0" b="0.3"/>
    <set line-width="2.0" line-join="round"/>
    <path d="M 10,30 L 20,20 L 40,10 z"/>
    <!-- <fill preserve="1"/> <set-source r="0.4" g="0.4" b="0.9"/> -->
    <stroke/>
  </sub>
</surface>
```

Copyright (c) 2021 Tobias Hoffmann

License: https://opensource.org/licenses/MIT

