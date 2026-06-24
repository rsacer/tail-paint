# Taiul Paint

Editor de pintura simples e fácil de usar. Criado com Qt e C++ para um consumo ultra-baixo de recursos e performance máxima.

Esse é um projeto pessoal, criado para uso próprio, que estou distribuindo livremente.

Visual inspirado no GNOME/Adwaita (tema escuro).

## Recursos

- **Ferramentas de desenho**: lápis (com antialiasing), borracha, linha,
  retângulo, elipse e balde de tinta
- **Seleção** retangular com cantos arredondados configuráveis
- **Múltiplas páginas** em abas (Ctrl+T)
- **Zoom** com Ctrl + roda do mouse, Ctrl +/−/0 e controle flutuante editável
- **Redimensionar a área de pintura** por alças de arraste ou pelo controle flutuante
- **Inserir imagens** com Ctrl+V ou pelo menu de contexto do canvas
- **Arrastar e soltar** imagens direto do gerenciador de arquivos
- Opção "colidir com a borda" para restringir imagens flutuantes ao canvas
- Seletor de cor, paleta rápida e espessura do traço ajustável
- Desfazer / refazer
- Abrir e salvar imagens (PNG, JPEG, BMP, GIF)
- Empacotamento como AppImage

## Atalhos

| Atalho   | Ação              |
|----------|-------------------|
| Ctrl+N   | Nova página       |
| Ctrl+T   | Nova aba          |
| Ctrl+O   | Abrir             |
| Ctrl+S   | Salvar            |
| Ctrl+V   | Colar imagem      |
| Ctrl+Z   | Desfazer          |
| Ctrl+Y   | Refazer           |
| Ctrl + + | Aumentar zoom     |
| Ctrl + − | Diminuir zoom     |
| Ctrl + 0 | Restaurar zoom    |

## Compilar

Requisitos: Qt 6 (Widgets, Svg), CMake 3.16+ e um compilador C++17.

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
./build/gnome-paint
```

## Gerar o AppImage

```bash
bash packaging/build-appimage.sh
```

## Estrutura

```
taiul-paint/
├── CMakeLists.txt
├── resources.qrc          # recursos embutidos (estilos, ícones, app icon)
├── icons/                 # ícones SVG das ferramentas
├── packaging/             # .desktop e script de build do AppImage
├── src/
│   ├── main.cpp           # ponto de entrada
│   ├── MainWindow.{h,cpp} # janela, barra de ferramentas e abas
│   ├── Canvas.{h,cpp}     # área de pintura (QImage + ferramentas)
│   └── IconLoader.{h,cpp} # renderização dos ícones SVG
└── styles/
    └── adwaita-dark.qss   # tema escuro Adwaita
```

## Créditos

Desenvolvido com a assistência do [Claude Code](https://claude.com/claude-code).
