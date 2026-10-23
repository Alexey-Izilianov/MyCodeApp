.pragma library
// Значки файлов (FileIcon): [буквы или "@значок LineIcon", цвет]. Цвета средние —
// читаются на светлой и тёмной теме. Библиотека общая для всех значков

const byName = {
    "cmakelists.txt": ["CM", "#d16a3a"],
    ".gitignore": ["@git", "#e2583e"], ".gitattributes": ["@git", "#e2583e"], ".gitmodules": ["@git", "#e2583e"],
    "dockerfile": ["D", "#2496ed"],
    "makefile": ["MK", "#8a8f99"],
    "license": ["©", "#c9a227"], "license.txt": ["©", "#c9a227"], "license.md": ["©", "#c9a227"],
    ".clang-format": ["@settings", "#8a8f99"], ".clang-tidy": ["@settings", "#8a8f99"],
    ".editorconfig": ["@settings", "#8a8f99"]
};

const byExtension = {
    c: ["C", "#5e8fd0"], h: ["H", "#9b7fd4"], hh: ["H", "#9b7fd4"], hpp: ["H", "#9b7fd4"], hxx: ["H", "#9b7fd4"],
    cpp: ["C++", "#5e8fd0"], cc: ["C++", "#5e8fd0"], cxx: ["C++", "#5e8fd0"], inl: ["C++", "#5e8fd0"],
    cs: ["C#", "#8b5fc7"], java: ["J", "#e0761c"], kt: ["K", "#8f6bff"], kts: ["K", "#8f6bff"],
    go: ["GO", "#00a5c8"], rs: ["RS", "#d08770"], swift: ["SW", "#f05138"], dart: ["DT", "#2fa7e0"],
    py: ["PY", "#3d7fc4"], pyw: ["PY", "#3d7fc4"], pyi: ["PY", "#3d7fc4"],
    rb: ["RB", "#cc342d"], php: ["PHP", "#777bb3"], lua: ["LUA", "#4a63c9"], r: ["R", "#2266b8"],
    gd: ["GD", "#478cbf"],
    js: ["JS", "#c9a227"], mjs: ["JS", "#c9a227"], cjs: ["JS", "#c9a227"], jsx: ["JSX", "#3fa6c4"],
    ts: ["TS", "#3178c6"], tsx: ["TSX", "#3178c6"], vue: ["V", "#41b883"],
    qml: ["QML", "#3fa65a"], qrc: ["QRC", "#3fa65a"], ui: ["UI", "#3fa65a"], pro: ["PRO", "#3fa65a"],
    html: ["<>", "#e0603a"], htm: ["<>", "#e0603a"], xml: ["<>", "#e37933"], xaml: ["<>", "#8b5fc7"],
    css: ["#", "#3d8fd6"], scss: ["S", "#c6538c"], sass: ["S", "#c6538c"], less: ["L", "#3d6db0"],
    json: ["{}", "#c9a227"], jsonc: ["{}", "#c9a227"],
    yaml: ["YML", "#cb4b4b"], yml: ["YML", "#cb4b4b"], toml: ["TML", "#9c6b4e"],
    md: ["M↓", "#4f8fd6"], markdown: ["M↓", "#4f8fd6"], rst: ["RST", "#8a8f99"],
    sql: ["SQL", "#d68a1c"], csv: ["CSV", "#3fa65a"], log: ["LOG", "#8a8f99"],
    sh: [">_", "#4eaa25"], bash: [">_", "#4eaa25"], zsh: [">_", "#4eaa25"],
    ps1: [">_", "#2f7fd0"], psm1: [">_", "#2f7fd0"], bat: [">_", "#8a8f99"], cmd: [">_", "#8a8f99"],
    cmake: ["CM", "#d16a3a"], pdf: ["PDF", "#d93025"], svg: ["SVG", "#e0a030"],
    ini: ["@settings", "#8a8f99"], cfg: ["@settings", "#8a8f99"], conf: ["@settings", "#8a8f99"],
    env: ["@settings", "#8a8f99"], properties: ["@settings", "#8a8f99"],
    png: ["@image", "#a070c8"], jpg: ["@image", "#a070c8"], jpeg: ["@image", "#a070c8"],
    gif: ["@image", "#a070c8"], bmp: ["@image", "#a070c8"], ico: ["@image", "#a070c8"],
    webp: ["@image", "#a070c8"],
    zip: ["@archive", "#b08a50"], "7z": ["@archive", "#b08a50"], rar: ["@archive", "#b08a50"],
    tar: ["@archive", "#b08a50"], gz: ["@archive", "#b08a50"],
    lock: ["@lock", "#8a8f99"], txt: ["@text", "#8a8f99"]
};

function specOf(name) {
    const lower = name.toLowerCase();
    return byName[lower] ?? byExtension[lower.split(".").pop()] ?? null;
}
