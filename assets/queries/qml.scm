; Подсветка QML (+ JavaScript внутри) для tree-sitter-qmljs.
; Правила те же, что в cpp.scm: вложенный захват перекрашивает внешний,
; для одного узла побеждает первый шаблон, предикаты не использовать.

(comment) @comment

[
  (string)
  (template_string)
  (regex)
] @string
(escape_sequence) @escape
(number) @number

[
  (true)
  (false)
  (null)
  (undefined)
  (this)
  (super)
] @constant

; Типы: объекты, импорты, типы свойств, встроенные компоненты
(ui_object_definition
  type_name: (_) @type)
(ui_object_definition_binding
  type_name: (_) @type)
(ui_import
  source: (_) @type)
(ui_property
  type: (_) @type)
(ui_inline_component
  name: (identifier) @type)

; Свойства и привязки: width: 100, anchors.fill: parent
(ui_binding
  name: (_) @property)
(ui_object_definition_binding
  name: (_) @property)
(ui_property
  name: (identifier) @property)
(ui_required
  name: (identifier) @property)

; Сигналы и функции
(ui_signal
  name: (identifier) @function)
(function_declaration
  name: (identifier) @function)
(method_definition
  name: (property_identifier) @function)
(call_expression
  function: (identifier) @function)
(call_expression
  function: (member_expression
    property: (property_identifier) @function))

[
  "as"
  "component"
  "default"
  "final"
  "import"
  "on"
  "override"
  "pragma"
  "property"
  "readonly"
  "required"
  "signal"
  "virtual"
  "async"
  "await"
  "break"
  "case"
  "catch"
  "class"
  "const"
  "continue"
  "delete"
  "do"
  "else"
  "export"
  "extends"
  "finally"
  "for"
  "function"
  "get"
  "if"
  "in"
  "instanceof"
  "let"
  "new"
  "of"
  "return"
  "set"
  "static"
  "switch"
  "throw"
  "try"
  "typeof"
  "var"
  "void"
  "while"
  "yield"
] @keyword
