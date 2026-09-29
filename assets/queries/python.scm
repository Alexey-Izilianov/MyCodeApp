; Подсветка Python для tree-sitter-python (M2-2).
; Правила те же, что в cpp.scm: вложенный захват перекрашивает внешний,
; для одного узла побеждает первый шаблон, предикаты не использовать.

(comment) @comment

(string) @string
(escape_sequence) @escape

[
  (integer)
  (float)
] @number

[
  (none)
  (true)
  (false)
] @constant

(decorator) @function

(function_definition
  name: (identifier) @function)
(call
  function: (identifier) @function)
(call
  function: (attribute
    attribute: (identifier) @function))

(class_definition
  name: (identifier) @type)
(type
  (identifier) @type)

[
  "and"
  "as"
  "assert"
  "async"
  "await"
  "break"
  "case"
  "class"
  "continue"
  "def"
  "del"
  "elif"
  "else"
  "except"
  "finally"
  "for"
  "from"
  "global"
  "if"
  "import"
  "in"
  "is"
  "lambda"
  "match"
  "nonlocal"
  "not"
  "or"
  "pass"
  "raise"
  "return"
  "try"
  "while"
  "with"
  "yield"
] @keyword
