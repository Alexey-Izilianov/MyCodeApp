; Подсветка C/C++ для tree-sitter-cpp (M2-2).
; Имена захватов: keyword, type, function, string, escape, number,
; constant, comment, preprocessor. Вложенный захват перекрашивает
; внешний (escape внутри строки); для одного узла побеждает первый шаблон.
; Предикаты (#match? и т.п.) не поддерживаются — не использовать.

(comment) @comment

[
  (string_literal)
  (raw_string_literal)
  (char_literal)
  (system_lib_string)
] @string

(escape_sequence) @escape

(number_literal) @number

[
  (true)
  (false)
  (null)
  (this)
] @constant

[
  "#include"
  "#define"
  "#if"
  "#ifdef"
  "#ifndef"
  "#else"
  "#elif"
  "#endif"
  (preproc_directive)
] @preprocessor

; Функции: объявления и вызовы
(function_declarator
  declarator: (identifier) @function)
(function_declarator
  declarator: (field_identifier) @function)
(function_declarator
  declarator: (qualified_identifier
    name: (identifier) @function))
(call_expression
  function: (identifier) @function)
(call_expression
  function: (field_expression
    field: (field_identifier) @function))
(call_expression
  function: (qualified_identifier
    name: (identifier) @function))
(template_function
  name: (identifier) @function)

[
  (primitive_type)
  (type_identifier)
  (sized_type_specifier)
  (auto)
  (namespace_identifier)
] @type

[
  "break"
  "case"
  "catch"
  "class"
  "co_await"
  "co_return"
  "co_yield"
  "concept"
  "const"
  "consteval"
  "constexpr"
  "constinit"
  "continue"
  "decltype"
  "default"
  "delete"
  "do"
  "else"
  "enum"
  "explicit"
  "extern"
  "final"
  "for"
  "friend"
  "goto"
  "if"
  "inline"
  "mutable"
  "namespace"
  "new"
  "noexcept"
  "operator"
  "override"
  "private"
  "protected"
  "public"
  "requires"
  "return"
  "sizeof"
  "static"
  "static_assert"
  "struct"
  "switch"
  "template"
  "thread_local"
  "throw"
  "try"
  "typedef"
  "typename"
  "union"
  "using"
  "virtual"
  "volatile"
  "while"
] @keyword
