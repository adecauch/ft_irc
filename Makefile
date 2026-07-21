# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: adecauch <adecauch@student.42.fr>          +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2025/10/27 09:10:07 by adecauch          #+#    #+#              #
#    Updated: 2026/07/20 19:03:37 by adecauch         ###   ########.fr        #
#                                                                              #
# **************************************************************************** #

GREEN = \033[0;32m
RED = \033[0;31m
RESET = \033[0m

CXX = c++
INC = -Iinclude
CXXFLAGS = -Wall -Wextra -Werror -std=c++98 $(INC)

NAME = ircserv

SRCS =	srcs/main.cpp \
		srcs/Client.cpp \
		srcs/Server.cpp \
		srcs/Parser.cpp \
		srcs/commands/commands.cpp \
		srcs/commands/Registration.cpp

OBJS = $(SRCS:.cpp=.o)

HEADERS =	include/Client.hpp \
			include/Server.hpp \
			include/Parser.hpp \
			include/NumericReplies.hpp \
			include/commands/commands.hpp

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)
#	@echo "$(GREEN)Compilation successful!$(RESET)"

$(OBJS): $(HEADERS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)
#	@echo "$(RED)Removed object files$(RESET)"

fclean: clean
	rm -f $(NAME)
#	@echo "$(RED)Removed executable$(RESET)"

re: fclean all

.PHONY: all clean fclean re
