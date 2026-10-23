; Разметка внутри абзацев Markdown (tree-sitter-markdown-inline)

(emphasis) @italic
(strong_emphasis) @bold
(strikethrough) @strike

(code_span) @code

[
  (link_text)
  (link_label)
  (image_description)
] @link

[
  (link_destination)
  (uri_autolink)
  (email_autolink)
] @url

(link_title) @string

[
  (emphasis_delimiter)
  (code_span_delimiter)
  (latex_span_delimiter)
] @punctuation

(image ["!" "[" "]" "(" ")"] @punctuation)
(inline_link ["[" "]" "(" ")"] @punctuation)
(shortcut_link ["[" "]"] @punctuation)

(latex_block) @code
(html_tag) @preprocessor

[
  (backslash_escape)
  (entity_reference)
  (numeric_character_reference)
] @escape
