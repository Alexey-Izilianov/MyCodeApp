; Вложенные языки Markdown. @inline — разметка абзаца (markdown-inline);
; @injection.content — код блока ```, язык которого назван в @injection.language

(inline) @inline
(pipe_table_cell) @inline

(fenced_code_block
  (info_string (language) @injection.language)
  (code_fence_content) @injection.content)
