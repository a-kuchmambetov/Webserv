NAME = webserv

CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++20
CPPFLAGS = -Iincludes -Isrc/Http -Isrc/Configs -Isrc/Server -Isrc/CGIs -Isrc/Shared
DEPFLAGS = -MMD -MP

SRC_SHARED =	src/Shared/UniqueFd.cpp

SRC_HTTP = src/Http/HttpTypes.cpp \
					 src/Http/HttpRequest.cpp

SRC_SERVERCONFIG = src/Configs/ServerConfig.cpp \
									 src/Configs/LocationConfig.cpp \
									 src/Configs/Tokenizer.cpp \
									 src/Configs/Parser.cpp \
									 src/Configs/Validator.cpp

SRC_SERVER = src/Server/WebServer.cpp \
						 src/Server/WebServer.serverLoop.cpp \
						 src/Server/Connection.cpp \
						 src/Server/Poller.cpp

SRC_CGI = src/CGIs/CgiRequest.cpp \
				src/CGIs/CgiResult.cpp


SRCS = main.cpp \
			$(SRC_SHARED) \
			$(SRC_HTTP) \
			$(SRC_SERVER) \
			$(SRC_SERVERCONFIG) \
			$(SRC_CGI)

OBJ_DIR = obj
OBJS = $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

RM = rm -f

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(DEPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) -r $(OBJ_DIR)

fclean: clean
	$(RM) $(NAME)
	$(RM) -r $(TEST_BIN_DIR)

re: fclean all

# --- Tests ---
TEST_DIR      = tests
TEST_BIN_DIR  = $(TEST_DIR)/bin
TEST_COMMON   = $(SRC_SERVERCONFIG) $(SRC_HTTP)


#TEST_CPPFLAGS = $(CPPFLAGS) -I$(TEST_DIR) -lgtest -lgtest_main -pthread

TEST_CXXFLAGS = $(CXXFLAGS)
TEST_CPPFLAGS = $(CPPFLAGS) -I$(TEST_DIR)
TEST_LDFLAGS  = -lgtest -lgtest_main -pthread


test: test-tokenizer test-parser test-validator

test-tokenizer: $(TEST_BIN_DIR)/tokenizer
	@cd $(TEST_DIR) && ./bin/tokenizer --gtest_color=yes

test-parser: $(TEST_BIN_DIR)/parser
	@cd $(TEST_DIR) && ./bin/parser --gtest_color=yes

test-validator: $(TEST_BIN_DIR)/validator
	@cd $(TEST_DIR) && ./bin/validator --gtest_color=yes

test-http-request: $(TEST_BIN_DIR)/http-request
	@cd $(TEST_DIR) && ./bin/http-request --gtest_color=yes


#$(TEST_BIN_DIR)/tokenizer: $(TEST_DIR)/test_tokenizer.cpp $(TEST_COMMON) | $(TEST_BIN_DIR)
#	$(CXX) $(TEST_CPPFLAGS) $(CXXFLAGS) $^ -o $@

#$(TEST_BIN_DIR)/parser: $(TEST_DIR)/test_parser.cpp $(TEST_COMMON) | $(TEST_BIN_DIR)
#	$(CXX) $(TEST_CPPFLAGS) $(CXXFLAGS) $^ -o $@

#$(TEST_BIN_DIR)/validator: $(TEST_DIR)/test_validator.cpp $(TEST_COMMON) | $(TEST_BIN_DIR)
#	$(CXX) $(TEST_CPPFLAGS) $(CXXFLAGS) $^ -o $@

#$(TEST_BIN_DIR)/http-request: $(TEST_DIR)/test_http_request.cpp $(TEST_COMMON) | $(TEST_BIN_DIR)
#	$(CXX) $(TEST_CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(TEST_BIN_DIR)/tokenizer: $(TEST_DIR)/test_tokenizer.cpp $(TEST_COMMON) | $(TEST_BIN_DIR)
	$(CXX) $(TEST_CPPFLAGS) $(TEST_CXXFLAGS) $^ -o $@ $(TEST_LDFLAGS)

$(TEST_BIN_DIR)/parser: $(TEST_DIR)/test_parser.cpp $(TEST_COMMON) | $(TEST_BIN_DIR)
	$(CXX) $(TEST_CPPFLAGS) $(TEST_CXXFLAGS) $^ -o $@ $(TEST_LDFLAGS)

$(TEST_BIN_DIR)/validator: $(TEST_DIR)/test_validator.cpp $(TEST_COMMON) | $(TEST_BIN_DIR)
	$(CXX) $(TEST_CPPFLAGS) $(TEST_CXXFLAGS) $^ -o $@ $(TEST_LDFLAGS)

$(TEST_BIN_DIR)/http-request: $(TEST_DIR)/test_http_request.cpp $(TEST_COMMON) | $(TEST_BIN_DIR)
	$(CXX) $(TEST_CPPFLAGS) $(TEST_CXXFLAGS) $^ -o $@ $(TEST_LDFLAGS)



$(TEST_BIN_DIR):
	mkdir -p $@

testclean:
	$(RM) -r $(TEST_BIN_DIR)

-include $(DEPS)

.PHONY: all clean fclean re test test-tokenizer test-parser test-validator testclean
