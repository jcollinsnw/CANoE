#!/bin/bash
# Minify the HTML/CSS/JS payload from index_html.h.bak (the readable source)
# and write the minified result to index_html.h (the compiled output).
#
# The .bak file is the true source — edit that, never index_html.h directly.
# Silently skips (exit 0) if npx is not available — build still works, just larger.
#
# Usage:  ./minify_index_html.sh firmware/accessory_node/index_html.h
# The Makefile calls this automatically before each compile target.

OUTFILE="$1"

if [[ -z "$OUTFILE" ]]; then
  echo "[minify] usage: $0 <path/to/index_html.h>" >&2
  exit 1
fi

SRCFILE="${OUTFILE}.bak"

if [[ ! -f "$SRCFILE" ]]; then
  echo "[minify] source file not found: $SRCFILE — skipping" >&2
  exit 0
fi

if ! command -v npx &>/dev/null; then
  echo "[minify] skipping — npx not found (install Node.js to enable)" >&2
  # Copy source as-is so the build still has a valid index_html.h
  cp "$SRCFILE" "$OUTFILE"
  exit 0
fi

TMPRAW="/tmp/idx_raw_$$.html"
TMPMIN="/tmp/idx_min_$$.html"
TMPERR="/tmp/idx_err_$$.txt"

cleanup() { rm -f "$TMPRAW" "$TMPMIN" "$TMPERR"; }
trap cleanup EXIT

# Extract the HTML payload from the source file
awk '/R"HTMLPAGE\(/{found=1; next} /\)HTMLPAGE"/{found=0; next} found' "$SRCFILE" > "$TMPRAW"

if [[ ! -s "$TMPRAW" ]]; then
  echo "[minify] R\"HTMLPAGE( block not found in $SRCFILE — copying source as-is" >&2
  cp "$SRCFILE" "$OUTFILE"
  exit 0
fi

# Minify — input is positional argument, not --input flag
if ! npx --yes html-minifier-terser \
    --collapse-whitespace \
    --remove-comments \
    --remove-optional-tags \
    --remove-redundant-attributes \
    --remove-script-type-attributes \
    --remove-style-link-type-attributes \
    --minify-css true \
    --minify-js true \
    --output "$TMPMIN" \
    "$TMPRAW" 2>"$TMPERR"; then
  echo "[minify] html-minifier-terser failed — copying source as-is" >&2
  [[ -s "$TMPERR" ]] && cat "$TMPERR" >&2
  cp "$SRCFILE" "$OUTFILE"
  exit 0
fi

if [[ ! -s "$TMPMIN" ]]; then
  echo "[minify] minifier produced empty output — copying source as-is" >&2
  cp "$SRCFILE" "$OUTFILE"
  exit 0
fi

# Rebuild the .h: keep the C++ header/footer from the source, splice in minified HTML
awk -v minfile="$TMPMIN" '
  /R"HTMLPAGE\(/ {
    print
    while ((getline line < minfile) > 0) printf "%s", line
    printf "\n"
    close(minfile)
    in_html = 1
    next
  }
  /\)HTMLPAGE"/ { in_html = 0 }
  !in_html       { print }
' "$SRCFILE" > "$OUTFILE"

orig=$(wc -c < "$SRCFILE" | tr -d ' ')
mini=$(wc -c < "$OUTFILE"  | tr -d ' ')
pct=$(( (orig - mini) * 100 / orig ))
echo "[minify] ${orig} → ${mini} bytes (${pct}% smaller)"
