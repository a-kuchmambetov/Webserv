NAME = webserv

CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++20
CPPFLAGS = -Iincludes -Isrc/Http -Isrc/Configs -Isrc/Server -Isrc/CGIs -Isrc/Shared
DEPFLAGS = -MMD -MP

SRC_SHARED =	src/Shared/UniqueFd.cpp

SRC_SERVERCONFIG = src/Configs/ServerConfig.cpp

SRC_SERVER = src/Server/WebServer.cpp \
						 src/Server/Connection.cpp \
						 src/Server/Poller.cpp


SRCS = main.cpp \
			$(SRC_SHARED) \
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

re: fclean all

-include $(DEPS)

.PHONY: all clean fclean re
