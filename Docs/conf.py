DOXYFILE = 'Doxyfile'
FAVICON = 'favicon.ico'
MAIN_PROJECT_URL = 'https://deat.tk/jazz2/'
SHOW_UNDOCUMENTED = True
VERSION_LABELS = True

STYLESHEETS = [
    'https://fonts.googleapis.com/css?family=Source+Sans+Pro:400,400i,600,600i%7CSource+Code+Pro:400,400i,600&subset=latin-ext',
    '../css/m-dark+documentation.compiled.css'
]

LINKS_NAVBAR2 = [
    (None, 'annotated', []),
    (None, 'files', [])
]

SEARCH_DOWNLOAD_BINARY = True
SEARCH_BASE_URL = "https://deat.tk/jazz2/docs/"
SEARCH_EXTERNAL_URL = "https://google.com/search?q=site:deat.tk+jazz2+docs+{query}"

# Code wrapped in DOXYGEN_ELLIPSIS() will get replaced by an (Unicode) ellipsis
# in the output; code wrapped in DOXYGEN_IGNORE() will get replaced by nothing.
# In order to make the same code compilable, add
#
#   #define DOXYGEN_ELLIPSIS(...) __VA_ARGS__
#   #define DOXYGEN_IGNORE(...) __VA_ARGS__
#
# to the snippet code.
def _doxygen_ignore(code: str):
    for macro, replace in [('DOXYGEN_ELLIPSIS(', '…'), ('DOXYGEN_IGNORE(', '')]:
        while macro in code:
            print("REPLACE:", macro)
            i = code.index(macro)
            depth = 1
            for j in range(i + len(macro), len(code)):
                if code[j] == '(': depth += 1
                elif code[j] == ')': depth -= 1
                if depth == 0: break
            assert depth == 0, "unmatched %s) parentheses in %s" % (macro, code)
            code = code[:i] + replace + code[j+1:]
    return code

M_CODE_FILTERS_PRE = {
    'C++': _doxygen_ignore
}
