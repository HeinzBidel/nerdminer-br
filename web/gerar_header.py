"""Gera src/painel_html.h a partir de web/painel.html (rode depois de mudar a pagina).
uso: python3 -I web/gerar_header.py
"""
import pathlib

raiz = pathlib.Path(__file__).resolve().parent.parent
html = (raiz / "web" / "painel.html").read_text(encoding="utf-8")
assert ")PAINEL\"" not in html
saida = (
    "// Gerado por web/gerar_header.py a partir de web/painel.html. Nao edite a mao.\n"
    "#ifndef PAINEL_HTML_H\n#define PAINEL_HTML_H\n\n"
    "static const char PAINEL_HTML[] PROGMEM = R\"PAINEL(" + html + ")PAINEL\";\n\n#endif\n"
)
(raiz / "src" / "painel_html.h").write_text(saida, encoding="utf-8")
print("src/painel_html.h:", len(html.encode("utf-8")), "bytes de pagina")
