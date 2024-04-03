# Components

## Cmake modules

| Module             | Purpose                                               | Dependencies  |
|--------------------|-------------------------------------------------------|---------------|
| instinct-core      | functional interfaces and utilities                   | N/A           |
| instinct-llm       | LLM provider integration, prompt templates, tokenizer | instinct-core |
| instinct-retrieval | RAG components, notebook for evaluation               | instinct-llm  |
| instinct-server    | HTTP server for chains                                | instinct-core |
| instinct-examples  | Reference apps and other samples                      | ALL           |



## Built-in components for Model I/O

### Input parsers - converting from `T` to `JSONContext`

| IInputParser<T> sub-class    | Input Type         | Notes                                                                                                                                          |
|------------------------------|--------------------|------------------------------------------------------------------------------------------------------------------------------------------------|
| PromptValueVariantInputPaser | PromptValueVariant | PromptValueVariant is a variant that accepts any possible values as a prompt value including `std::string`, `Message`, `MessageList` and more. |

### LLM and ChatModel

`LLM` and `ChatModel` are not `StepFunction`s on their own. Related `StepFunctionPtr`s can be created using `AsModelFunction` method on them.

These model classes expect a `PromptValue` message type from context and produce a `Generation` message type.

Current available model classes are listed in following table.

| Model sub-class | Notes                                                                                                                                   |
|-----------------|-----------------------------------------------------------------------------------------------------------------------------------------|
| OllamaLLM       | Text completion API of Ollama                                                                                                           |
| ChatLLM         | Chat completion API of Ollama                                                                                                           |
| ChatOpenAI      | Chat completion API of OpenAI. It can be used with any OpenAI compatible server like [nitro](https://github.com/janhq/nitro/tree/main). |



### Prompt templates

Subclasses of `IPromptTemplate` are `StepFunction`. You can directly create chain with them. They are expecting `JSONMappingContext` from context and producing `PromptValue` message, and are thus often put before model functions.

| IPromptTemplate subclass | Notes                                                     |
|--------------------------|-----------------------------------------------------------|
| PlainPromptTemplate      | To create a single message prompt using string template.  |
| PlainChatPromptTemplate  | To create a list of message prompts with string template. |
| FewShotPromptTemplate    | To create a single message containing few-shot examples.  |


### Output parsers - converting from `JSONContext` to user-defined `T`.

Subclasses of `IOutputParser<T>` are not `StepFunction`. But specials operator overrides are implemented so that they can be concatenated with `StepFunctionPtr`.

Output parsers expect a `Generation` message from context. Some output parser may relay on certain format instruction as part of prompt value. A `StepFunction` for producing a string of format instruction can be generated with `AsInstructorFunction`, and the output should be used in a prompt template. To put these pieces together, common practice should be constructing a `MappingStepFunction` that containing format instruction, which is followed by a prompt template instance consuming map data.   


| IOutputParser<T> subclass       | Output type         | Note                                                     |
|---------------------------------|---------------------|----------------------------------------------------------|
| StringOutputParser              | std::string         | To extract string value from a `Generation` message.     |
| MultilineGenerationOutputParser | MultilineGeneration | To extract multi-line texts from a `Generation` message. |


More output parsr classes will be added in near future.

### Chat memory

Subclass of `IChatMemory` are short memory for LLMs. They persist conversation records, which will be used in next round of prompting.

As there are two aspects regarding `memory`, two distinct `StepFunctionPtr` can be created with a single memory class. That's `AsLoadMemoryFunction` to obtain a `StepFunctionPtr` that load message list to context, and  `AsSaveMemoryFunction` to save prompt and answer pair into internal storage.


| IChatMemory subclass | Note                                                |
|----------------------|-----------------------------------------------------|
| EphemeralChatMemory  | To save data in memory with no persistence involved |

More chat memory implementation will be added in near future.

## RAG related classes

### DocStore and VectorStore

`IDocStore` and `IVectorStore` are primary database for documents and their embeddings. A `IVectorStore` instance is similar to a `IDocStore` instance in terms of CURD operations with documents, but it has extra capabilities to embed documents as vectors and perform vector search on vector data.

Currently, only `DuckDBDocStore` and `DuckDBVectorStore` are implemented for local usage. `DuckDBVectorStore` handles documents recall in a brute-force way that scan entire table with embeddings using `array_cosine_simliarity` function. Performance would be degraded if database is large enough. In benchmarks with SIFT-1M dataset, a single recall would result round trip of more than three seconds on my local machine with 10-core M1 MAX CPU. Of course, optimizations could be done using ANNs like faiss, but it's totally reasonable for dataset less than 1M with limited concurrency.    

### Retrievers

The most important role in RAG pipeline is `Retriever`.  Some common retrieving patterns are supported already.

|                                           | What are stored in DocStore          | Query handling                        | What are stored in VectorStore                 | Returned Docs                            |
|-------------------------------------------|--------------------------------------|---------------------------------------|------------------------------------------------|------------------------------------------|
| VectorStoreRetriever                      | original doc                         | raw query                             | embedding of original doc                      | original doc                             |
| CMV, ChunkedMultiVectorRetriever          | original doc or chunked parent parts | raw query                             | embedding of chunked children of a parent part | original doc or chunked parent parts     |
| CMV with summary as guidance              | original doc                         | raw query                             | embedding of doc summary                       | original doc                             |
| CMV with hypothetical queries as guidance | original doc                         | raw query                             | embedding of generated questions               | original doc                             |
| AutoRetriever (WIP)                       | original doc                         | raw query + generated metadata filter | embedding of original doc                      | original doc                             |
| MultiQueryRetriever                       | original doc                         | multiple generated queries            | embedding of original doc                      | original doc                             |
| RerankingRetriever (WIP)                  | original doc                         | raw query                             | embedding of original doc                      | original docs are re-ordered or filtered |


Please refer to following links for more exemplary usage for retrievers.

* [doc-agent app](../modules/instinct-examples/doc-agent)
* [retrieval test](../modules/instinct-retrieval/test/retrieval)


### Data Ingestor

`Ingestor` are utility classes that load document data from data sources into retrievers. 

| IIngestor sub-class   | Data source                   | Notes                                                                                                                     |
|-----------------------|-------------------------------|---------------------------------------------------------------------------------------------------------------------------|
| SingleFileIngestor    | single plain text file        | TXT,MD,HTML are supported with corresponding configurations.                                                              |
| PDFFileIngestor       | single pdf file               | Text are extracted and merged from text page objects.                                                                     |
| ParquetFileIngestor   | single source of parquet file | Both local file and remote url are supported. Columns mapping should be provided.                                         |
| DirectoryTreeIngestor | a local directory             | Files are globbed, recursively if requested. Selected files are then passed to another ingestor for single file handling. |
