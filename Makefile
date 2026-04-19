NAME = webserv

CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++20 -Iincludes
CPPFLAGS = -Iincludes -Isrc/Http -Isrc/Configs -Isrc/Server -Isrc/CGIs -Isrc/Shared -MMD -MP

SRCS = main.cpp
OBJS = $(SRCS:.cpp=.o)
DEPS = $(OBJS:.o=.d)

RM = rm -f

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(DEPS)

fclean: clean
	$(RM) $(NAME)

re: fclean all

-include $(DEPS)

.PHONY: all clean fclean re