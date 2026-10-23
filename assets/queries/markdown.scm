; Подсветка блоков Markdown (tree-sitter-markdown).
; Разметку внутри абзацев красит markdown-inline.scm, код в ``` — грамматика
; его языка (markdown-injections.scm). Вложенный захват перекрашивает внешний.

(atx_heading) @heading
(setext_heading) @heading

[
  (indented_code_block)
  (fenced_code_block)
] @code

[
  (fenced_code_block_delimiter)
  (info_string)
] @punctuation

[
  (list_marker_plus)
  (list_marker_minus)
  (list_marker_star)
  (list_marker_dot)
  (list_marker_parenthesis)
  (task_list_marker_checked)
  (task_list_marker_unchecked)
  (block_quote_marker)
  (thematic_break)
] @punctuation

(block_quote) @quote

[
  (pipe_table_delimiter_row)
  "|"
] @punctuation

(pipe_table_header) @bold

(link_label) @link
(link_destination) @url
(link_title) @string

[
  (minus_metadata)
  (plus_metadata)
] @comment

(html_block) @preprocessor
(backslash_escape) @escape
