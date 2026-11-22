add_executable(test_tokenizer test_tokenizer.c)
target_link_libraries(test_tokenizer PRIVATE minishell_core)
add_test(NAME tokenizer COMMAND test_tokenizer)

add_executable(test_parser test_parser.c)
target_link_libraries(test_parser PRIVATE minishell_core)
add_test(NAME parser COMMAND test_parser)
