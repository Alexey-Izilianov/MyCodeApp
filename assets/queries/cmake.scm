; Подсветка CMake (tree-sitter-cmake). Вложенный захват перекрашивает внешний:
; ${VAR} внутри строки — цветом переменной.

[
  (line_comment)
  (bracket_comment)
] @comment

[
  (quoted_argument)
  (bracket_argument)
] @string

(escape_sequence) @escape

(variable_ref) @property

[
  (if) (elseif) (else) (endif)
  (foreach) (endforeach)
  (while) (endwhile)
  (function) (endfunction)
  (macro) (endmacro)
  (block) (endblock)
] @keyword

(function_command (argument_list . (argument) @function))
(macro_command (argument_list . (argument) @function))
(normal_command (identifier) @function)

; Ключевые слова команд: PRIVATE, STATIC, ON, GIT_TAG...
((unquoted_argument) @constant
  (#match? @constant "^[A-Z@][A-Z0-9_]+$"))

((unquoted_argument) @number
  (#match? @number "^[0-9][0-9.]*$"))
