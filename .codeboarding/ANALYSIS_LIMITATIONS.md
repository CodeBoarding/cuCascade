# cuCascade: best-effort analysis preview

This is a completed CodeBoarding run, not a complete compiler-resolved dependency graph.
Load `analysis.json` at https://app.codeboarding.org to inspect the diagram.

- Source revision: `1a9281093a0fe49634594d541d7ce327129f53d6`.
- No project build was run for this analysis. Clangd used its default compiler context because the repository had no compilation database or root clangd configuration.
- Static analysis processed 143 C++/CUDA files and 2 Python files. The C++ pass retained 1,037 relationships and 439 hierarchy entries.
- Clangd reported 2,424 C++/CUDA errors, including missing project and dependency headers. Relationships requiring that unavailable context can be absent or incomplete.
- Optional template supplementation stopped because no compilation database was available. Existing relationships were retained; unresolved template targets were not guessed.
- `file_coverage.json` reports 145 analyzed files out of 227 text files. “Analyzed” means processed, not fully resolved. The other 82 files comprise 53 excluded by ignore rules and 29 categorized as other.
- Macros, inactive preprocessor branches, CUDA code and template instantiations can have incomplete coverage. LLM-written component descriptions are interpretations, not verified runtime behavior.
- Detailed compiler diagnostics and the template-limit warning are retained in the local static analysis cache and run log. Do not publish the pickle cache or logs as portable demo assets.

This preview is suitable for evaluating the first-pass architecture diagram. It is not suitable for proving that a relationship or dependency does not exist.
