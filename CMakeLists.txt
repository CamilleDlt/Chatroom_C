cmake_minimum_required(VERSION 3.30)
project(Chatroom_C C)

set(CMAKE_C_STANDARD 11)

add_executable(Chatroom_C
    client.c
    server.c
    client_gui.c)
