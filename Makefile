NAME = webserv

CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++20
CPPFLAGS = -Iincludes -Isrc/Http -Isrc/Configs -Isrc/Server -Isrc/CGIs -Isrc/Shared
DEPFLAGS = -MMD -MP

SRC_SHARED =	src/Shared/UniqueFd.cpp

SRC_HTTP = src/Http/HttpTypes.cpp

SRC_SERVERCONFIG = src/Configs/ServerConfig.cpp \
									 src/Configs/LocationConfig.cpp \
									 src/Configs/Tokenizer.cpp \
									 src/Configs/Parser.cpp \
									 src/Configs/Validator.cpp

SRC_SERVER = src/Server/WebServer.cpp \
						 src/Server/Connection.cpp \
						 src/Server/Poller.cpp


SRCS = main.cpp \
			$(SRC_SHARED) \
			$(SRC_HTTP) \
			$(SRC_SERVER) \
			$(SRC_SERVERCONFIG)

OBJS = $(SRCS:.cpp=.o)
DEPS = $(OBJS:.o=.d)

RM = rm -f

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(DEPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(DEPS)

fclean: clean
	$(RM) $(NAME)
	$(RM) -r $(TEST_BIN_DIR)

re: fclean all

# --- Tests ----------------------------------------------------------------
# Each component gets its own binary under tests/bin/ and is run from
# tests/ so fixture paths stay relative (e.g. "fixtures/valid_spaced.conf").
# Test sources reuse $(SRC_SERVERCONFIG) + $(SRC_HTTP) + the project's
# $(CPPFLAGS)/$(CXXFLAGS) — no flag drift, no duplicated source lists.

TEST_DIR      = tests
TEST_BIN_DIR  = $(TEST_DIR)/bin
TEST_COMMON   = $(SRC_SERVERCONFIG) $(SRC_HTTP)
TEST_CPPFLAGS = $(CPPFLAGS) -I$(TEST_DIR)

test: test-tokenizer test-parser test-validator

test-tokenizer: $(TEST_BIN_DIR)/tokenizer
	@cd $(TEST_DIR) && ./bin/tokenizer

test-parser: $(TEST_BIN_DIR)/parser
	@cd $(TEST_DIR) && ./bin/parser

test-validator: $(TEST_BIN_DIR)/validator
	@cd $(TEST_DIR) && ./bin/validator

$(TEST_BIN_DIR)/tokenizer: $(TEST_DIR)/test_tokenizer.cpp $(TEST_COMMON) | $(TEST_BIN_DIR)
	$(CXX) $(TEST_CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(TEST_BIN_DIR)/parser: $(TEST_DIR)/test_parser.cpp $(TEST_COMMON) | $(TEST_BIN_DIR)
	$(CXX) $(TEST_CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(TEST_BIN_DIR)/validator: $(TEST_DIR)/test_validator.cpp $(TEST_COMMON) | $(TEST_BIN_DIR)
	$(CXX) $(TEST_CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(TEST_BIN_DIR):
	mkdir -p $@

testclean:
	$(RM) -r $(TEST_BIN_DIR)

-include $(DEPS)

.PHONY: all clean fclean re test test-tokenizer test-parser test-validator testclean
