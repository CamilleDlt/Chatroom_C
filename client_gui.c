#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <raylib.h>

#define MAX_LEN 1000
#define MAX_MESSAGES 100
#define MAX_MESSAGE_LENGTH 1000
#define SCREEN_WIDTH 900
#define SCREEN_HEIGHT 1000
#define INPUT_HEIGHT 50
#define CHAT_MARGIN 10
#define FONT_SIZE 30

double lastBackspaceTime = 0;
double backspaceHoldStartTime = 0;
bool isBackspaceHeld = false;
const double BACKSPACE_DELAY = 0.5;      // Initial delay before repeat starts (in seconds)
const double BACKSPACE_REPEAT = 0.03;

typedef struct User {
    char name[100];
} User;

typedef struct {
    char text[MAX_MESSAGE_LENGTH];
    bool isOwn;  // True if mes### 5.4 rest TBDsage is from current user
} ChatMessage;


// Global variables

// Network / Thread related
int socketClient; // Stores the socket connection identifier for client-server connection
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex for thread-safe access

// Message storage
ChatMessage messages[MAX_MESSAGES]; // Array storing chat history
int messageCount = 0; // Number of stored messages

// Input handling
char inputBuffer[MAX_LEN] = {0}; // Stores the current message being typed
int inputLength = 0; // Tracks length of current input message

// GUI Layout
Rectangle inputBox; // Defines the position and size of the input box
Vector2 inputTextPosition; // Tracks length of current input message

// Scrolling management
Vector2 scrollPosition = {0}; // Tracks scroll position of message view
float scrollSpeed = 30.0f; // Determines how fast scrolling moves


// Function to add a new message to the chat
void AddMessage(const char* text, const bool isOwn) {

    // Lock the mutex for thread safety
    pthread_mutex_lock(&mutex);

    // While message buffer isn't full
    if (messageCount >= MAX_MESSAGES) {

        // Shift messages up by one to make room for new message
        for (int i = 0; i < MAX_MESSAGES - 1; i++) {
            memcpy(&messages[i], &messages[i + 1], sizeof(ChatMessage));
        }
        messageCount = MAX_MESSAGES - 1;
    }

    // Adds the new message in the storage array
    strncpy(messages[messageCount].text, text, MAX_MESSAGE_LENGTH - 1);
    messages[messageCount].text[MAX_MESSAGE_LENGTH - 1] = '\0';
    messages[messageCount].isOwn = isOwn;
    messageCount++;

    // Unlock the mutex after adding the message
    pthread_mutex_unlock(&mutex);
}

// Thread function that continuously listens for server messages, and adds the messages in the array
void *listen_to_server() {
    char buffer[MAX_LEN]; //Temporary storage for incoming messages

    // Infinite loop for continuous listening (exits on error or disconnection)
    while (1) {

        // recv() waits for data from the server (incoming message is written in the buffer)
        const ssize_t receiver = recv(socketClient, buffer, sizeof(buffer) - 1, 0);

        if (receiver <= 0) {
            AddMessage("Disconnected from the server.", false);
            break;
        }

        // We add a null terminator to end the message
        buffer[receiver] = '\0';

        // We show the received message in the UI
        AddMessage(buffer, false);
    }
    return NULL;
}

// Function to handle text input
void HandleTextInput() {
    const double currentTime = GetTime();

    // Handling regular character input
    int key = GetCharPressed(); // retrieves user input

    while (key > 0) {
        // Checks correct unicode range
        if (inputLength < MAX_LEN - 1 && key >= 32 && key <= 125) {
            inputBuffer[inputLength] = (char)key;
            inputLength++;
        }
        // Reads next key press
        key = GetCharPressed();
    }

    // Handling backspace (deleting a character)
    if (IsKeyPressed(KEY_BACKSPACE) && inputLength > 0) {
        if (inputLength > 0) {
            inputLength--;
            inputBuffer[inputLength] = '\0';
        }
        isBackspaceHeld = true;
        backspaceHoldStartTime = currentTime;
        lastBackspaceTime = currentTime;

    } else if (IsKeyDown(KEY_BACKSPACE)) {
        // If backspace is held down
        if (isBackspaceHeld) {
            const double timeHeld = currentTime - backspaceHoldStartTime;

            // After initial delay, start repeating
            if (timeHeld > BACKSPACE_DELAY) {
                // Check if it's time for another deletion
                if (currentTime - lastBackspaceTime >= BACKSPACE_REPEAT) {
                    if (inputLength > 0) {
                        inputLength--;
                        inputBuffer[inputLength] = '\0';
                        lastBackspaceTime = currentTime;
                    }
                }
            }
        }
        else {
            isBackspaceHeld = false;
        }
    }

    // Handling enter (sending a message)
    if (IsKeyPressed(KEY_ENTER) && inputLength > 0) { //also checking message isn't empty

        inputBuffer[inputLength] = '\0'; // null terminator at the end of message buffer

        // Sending message to server with error handling
        if (send(socketClient, inputBuffer, inputLength, 0) < 0) {
            AddMessage("Error when sending message.", false);
        }

        // Clear input buffer
        memset(inputBuffer, 0, MAX_LEN);
        inputLength = 0;
    }
}

int main() {
    //User creation
    User user;
    printf("Enter your name: ");
    fgets(user.name, sizeof(user.name), stdin); //retrieves name from stdin
    user.name[strcspn(user.name, "\n")] = '\0'; //adds the string end character to the name

    // Socket setup
    socketClient = socket(AF_INET, SOCK_STREAM, 0); //We create a socket client (Address Family Internet domain & TCP socket)

    if (socketClient < 0) {
        //socket() returns a negative value when an error is encountered
        perror("Error when creating socket.");
        exit(EXIT_FAILURE);
    }


    struct sockaddr_in addrClient = {0}; //Custom structure from in.h for configuring a socket connection
    addrClient.sin_addr.s_addr = inet_addr("127.0.0.1"); //Server address
    addrClient.sin_family = AF_INET; //domain --> Address Family Internet (IPv4)
    addrClient.sin_port = htons(30001); //ensures correct "network byte order" (little-endian / big-endian relation)

    //Testing connection between socket and the address specified in the client config
    if (connect(socketClient, (struct sockaddr *)&addrClient, sizeof(addrClient)) < 0) {
        perror("Connection error.");
        close(socketClient);
        exit(EXIT_FAILURE);
    }

    // Testing the initial data send (sending the user's name)
    if (send(socketClient, &user, sizeof(user), 0) < 0) {
        perror("Error when sending the name.");
        close(socketClient);
        exit(EXIT_FAILURE);
    }

    // Server listener thread structure
    pthread_t listen_thread;

    // Creating a thread and calling listen_to_server method
    // If connection fails (return is != 0) then the program exits.
    if (pthread_create(&listen_thread, NULL, listen_to_server, NULL) != 0) {
        perror("Error creating listening thread.");
        close(socketClient);
        exit(EXIT_FAILURE);
    }

    // Initialize raygui window with custom dimensions (defined at the start of file)
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Chat Client");
    SetTargetFPS(60);

    // Initialize input box
    inputBox = (Rectangle){ CHAT_MARGIN, SCREEN_HEIGHT - INPUT_HEIGHT - CHAT_MARGIN,
                           SCREEN_WIDTH - 2 * CHAT_MARGIN, INPUT_HEIGHT };
    inputTextPosition = (Vector2){ inputBox.x + 5, inputBox.y + 10 };

    // Main chat loop
    while (!WindowShouldClose()) { //Runs until the window closes (ESC or X button)

        // Handles keyboard input for chat messages
        HandleTextInput();

        // Handles scrolling
        if (GetMouseWheelMove() != 0) { //Detects mouse wheel mouvement
            scrollPosition.y += GetMouseWheelMove() * scrollSpeed;

            // Calculates max scroll limit
            float maxScroll = (float) messageCount * 25 - (SCREEN_HEIGHT - INPUT_HEIGHT - 2 * CHAT_MARGIN);

            // Enforce scroll boundaries/limits
            if (maxScroll < 0) maxScroll = 0;
            if (scrollPosition.y < -maxScroll) scrollPosition.y = -maxScroll;
            if (scrollPosition.y > 0) scrollPosition.y = 0;
        }

        // Drawing the window
        BeginDrawing();
        ClearBackground(RAYWHITE); // Clears screen using a white background

        // Drawing messages
        pthread_mutex_lock(&mutex); // Locks mutex for thread safety
        float y = SCREEN_HEIGHT - INPUT_HEIGHT - CHAT_MARGIN * 2 + scrollPosition.y;

        for (int i = messageCount - 1; i >= 0; i--) { // Loops through messages from newest to oldest

            // Different color depending on whom the message is from.
            const Color msgColor = messages[i].isOwn ? SKYBLUE : LIGHTGRAY;

            const float textWidth = (float) MeasureText(messages[i].text, FONT_SIZE);

            // Right align own messages and left align others' messages
            const float x = messages[i].isOwn ? SCREEN_WIDTH - textWidth - CHAT_MARGIN * 2 : CHAT_MARGIN;

            // Drawing message bubble
            DrawRectangle( (int)(x - 5), (int)(y - 25), (int)(textWidth + 10), 30, msgColor);

            // Drawing message content (within the bubble)
            DrawText(messages[i].text, (int) x, (int) y - 20, 30, BLACK);

            y -= 35; // Moves up for next message
            if (y < -30) break;  // Stop if message isn't in view
        }
        // Release lock after displaying message
        pthread_mutex_unlock(&mutex);

        // Draw input box
        DrawRectangleRec(inputBox, LIGHTGRAY); // box background
        DrawRectangleLinesEx(inputBox, 1, DARKGRAY); // box border
        DrawText(inputBuffer, (int) inputTextPosition.x, (int) inputTextPosition.y, FONT_SIZE, BLACK); // box text

        // End the drawing phase
        EndDrawing();
    }

    // Cleanup
    CloseWindow();
    pthread_cancel(listen_thread);
    pthread_join(listen_thread, NULL);
    close(socketClient);

    return 0;
}