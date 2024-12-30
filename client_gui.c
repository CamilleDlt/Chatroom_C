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
#define RED_CODE "\033[31m"
#define GREEN_CODE "\033[32m"
#define RESET_CODE "\033[0m"

typedef struct User {
    char name[100];
} User;

typedef struct {
    char* text;
    bool isOwn;  // True if message is from current user
    Color color;
    bool isServerMessage;
    struct ChatMessage* next;
} ChatMessage;

typedef struct {
    char* cleanText;
    Color messageColor;
    bool isServerMessage;
} CleanedMessage;


// Input handling
typedef struct {
    char* buffer;
    int capacity;
    int length;
    int gapStart;
    int gapSize;
} InputBuffer;


// Global variables

User user;  // Store the current user globally

// Network / Thread related
int socketClient; // Stores the socket connection identifier for client-server connection
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex for thread-safe access
pthread_t listen_thread; // Listening thread

// Message storage
ChatMessage** messages;// Array storing chat history
int messageCount = 0; // Number of stored messages
InputBuffer* inputBuffer;

// GUI Layout
Rectangle inputBox; // Defines the position and size of the input box
Vector2 inputTextPosition; // Tracks length of current input message

// Scrolling management
Vector2 scrollPosition = {0}; // Tracks scroll position of message view
float scrollSpeed = 30.0f; // Determines how fast scrolling moves

//Global Variables for long backspace press
double lastBackspaceTime = 0;
double backspaceHoldStartTime = 0;
bool isBackspaceHeld = false;
const double BACKSPACE_DELAY = 0.5;      // Initial delay before repeat starts (in seconds)
const double BACKSPACE_REPEAT = 0.03;

//Global Variables for visual cursor
int cursorPosition = 0;           // Current cursor position in the text
double lastCursorBlink = 0;       // Time tracking for cursor blink
bool showCursor = true;           // Cursor blink state
const double CURSOR_BLINK_RATE = 0.5; // Cursor blink rate in seconds

//Global Variables for arrow key presses
double lastArrowTime = 0;
double arrowHoldStartTime = 0;
bool isLeftArrowHeld = false;
bool isRightArrowHeld = false;
const double ARROW_DELAY = 0.5;      // Initial delay before repeat starts
const double ARROW_REPEAT = 0.05;     // Time between repeated moves


CleanedMessage* cleanServerMessage(const char* rawMessage) {
    CleanedMessage* result = malloc(sizeof(CleanedMessage));
    result->cleanText = malloc(sizeof(char) * MAX_LEN);

    result->messageColor = LIGHTGRAY;  // Default color
    result->isServerMessage = false;
    memset(inputBuffer->buffer, 0, MAX_LEN);  // Initialize buffer to zeros

    if (strstr(rawMessage, "SERVER: ") != NULL) {
        result->isServerMessage = true;
    }

    // Check if it's a server message (contains "connected" or "disconnected")
    if (strstr(rawMessage, "disconnected") != NULL){
        result->messageColor = RED;  // Server disconnection messages in red
    } else if (strstr(rawMessage, "connected") != NULL){
        result->messageColor = GREEN;  // server connection messages in green
    }

    // Clean the message by copying only visible characters
    int cleanIndex = 0;
    int i = 0;
    while (rawMessage[i] != '\0' && cleanIndex < MAX_MESSAGE_LENGTH - 1) {
        if (rawMessage[i] == '\033') {
            while (rawMessage[i] != 'm' && rawMessage[i] != '\0') {
                i++;
            }
            if (rawMessage[i] == 'm') i++;
            continue;
        }
        result->cleanText[cleanIndex++] = rawMessage[i++];
    }
    result->cleanText[cleanIndex] = '\0';

    return result;
}

// Function to insert character at cursor position
void insertCharacter(const int ch) {
    if (inputBuffer->length < MAX_LEN - 1) {
        // Shift characters right to make room
        for (int i = inputBuffer->length; i > cursorPosition; i--) {
            inputBuffer->buffer[i] = inputBuffer->buffer[i - 1];
        }
        inputBuffer->buffer[cursorPosition] = (char)ch;
        inputBuffer->length++;
        cursorPosition++;
    }
}

// Function to delete character at cursor position
void deleteCharacter() {
    if (cursorPosition < inputBuffer->length) {
        // Shift characters left to close gap
        for (int i = cursorPosition; i < inputBuffer->length - 1; i++) {
            inputBuffer->buffer[i] = inputBuffer->buffer[i + 1];
        }
        inputBuffer->length--;
        inputBuffer->buffer[inputBuffer->length] = '\0';  // Explicit null termination
    }
}

// Function to handle backspace at cursor position
void backspaceCharacter() {
    if (cursorPosition > 0) {
        cursorPosition--;
        for (int i = cursorPosition; i < inputBuffer->length - 1; i++) {
            inputBuffer->buffer[i] = inputBuffer->buffer[i + 1];
        }
        inputBuffer->length--;
        inputBuffer->buffer[inputBuffer->length] = '\0';  // Explicit null termination
    }
}

// Function to add a new message to the chat
void addMessage(const char* text, const bool isOwn) {
    if (!messages) {
        return;
    }

    // Lock the mutex for thread safety
    pthread_mutex_lock(&mutex);

    // While message buffer isn't full
    if (messageCount >= MAX_MESSAGES) {

        // Shift messages up by one to make room for new message
        for (int i = 0; i < MAX_MESSAGES - 1; i++) {
            messages[i] = messages[i + 1];
        }
        messageCount = MAX_MESSAGES - 1;
    }

    // Prepare message properties
    Color messageColor = SKYBLUE;
    bool isServerMessage = false;
    const char* messageText = text;

    CleanedMessage* cleaned = NULL;
    // Process server messages if not own message
    if (!isOwn) {
        cleaned = cleanServerMessage(text);
        messageText = cleaned->cleanText;
        messageColor = cleaned->messageColor;
        isServerMessage = cleaned->isServerMessage;
    }

    // Copy message text and set properties
    strncpy(messages[messageCount]->text, messageText, MAX_MESSAGE_LENGTH - 1);
    messages[messageCount]->text[MAX_MESSAGE_LENGTH - 1] = '\0';
    messages[messageCount]->isOwn = isOwn;
    messages[messageCount]->color = messageColor;
    messages[messageCount]->isServerMessage = isServerMessage;

    // Free cleaned message if it was created
    if (!isOwn) {
        free(cleaned->cleanText);
        free(cleaned);
    }

    messageCount++;
    // Unlock the mutex after adding the message
    pthread_mutex_unlock(&mutex);
}

// Thread function that continuously listens for server messages, and adds the messages in the array
void *listen_to_server() {
    if (!messages) return NULL;
    char buffer[MAX_LEN]; //Temporary storage for incoming messages

    // Infinite loop for continuous listening (exits on error or disconnection)
    while (1) {
        // recv() waits for data from the server (incoming message is written in the buffer)
        const ssize_t receiver = recv(socketClient, buffer, sizeof(buffer) - 1, 0);

        if (receiver <= 0) {
            addMessage("Disconnected from the server.", false);
            break;
        }

        // We add a null terminator to end the message
        buffer[receiver] = '\0';

        // We show the received message in the UI
        addMessage(buffer, strstr(buffer, user.name) == buffer);  // true if message starts with our name
    }
    // Thread terminates when connection drops or program exits
    return NULL; // Required for pthread function signature
}

// Cleanup method, freeing all the possible memory allocations called in this program
void cleanupAndExit() {
    CloseWindow();
    pthread_cancel(listen_thread);
    pthread_join(listen_thread, NULL);
    close(socketClient);

    for (int i= 0; i < MAX_MESSAGES; i++) {
        free(messages[i]->text);
        free(messages[i]);
    }
    free(messages);
    free(inputBuffer->buffer);
    free(inputBuffer);
    exit(EXIT_SUCCESS);
}

// Function to handle text input
void handleTextInput() {
    // Check for null pointers
    if (!inputBuffer || !inputBuffer->buffer) {
        return;
    }

    const double currentTime = GetTime(); //time variable used for backspace delays

    // Update cursor blink
    if (currentTime - lastCursorBlink >= CURSOR_BLINK_RATE) {
        showCursor = !showCursor;
        lastCursorBlink = currentTime;
    }

    if (IsKeyPressed(KEY_LEFT)) {
        if (cursorPosition > 0) {
            cursorPosition--;
            showCursor = true;
            lastCursorBlink = currentTime;
        }
        isLeftArrowHeld = true;
        arrowHoldStartTime = currentTime;
        lastArrowTime = currentTime;
    }
    else if (IsKeyDown(KEY_LEFT)) {
        if (isLeftArrowHeld) {
            const double timeHeld = currentTime - arrowHoldStartTime;
            if (timeHeld > ARROW_DELAY) {
                if (currentTime - lastArrowTime >= ARROW_REPEAT) {
                    if (cursorPosition > 0) {
                        cursorPosition--;
                        showCursor = true;
                        lastCursorBlink = currentTime;
                        lastArrowTime = currentTime;
                    }
                }
            }
        }
    }
    else {
        isLeftArrowHeld = false;
    }

    // Handle right arrow key with repeat
    if (IsKeyPressed(KEY_RIGHT)) {
        if (cursorPosition < inputBuffer->length) {
            cursorPosition++;
            showCursor = true;
            lastCursorBlink = currentTime;
        }
        isRightArrowHeld = true;
        arrowHoldStartTime = currentTime;
        lastArrowTime = currentTime;
    }
    else if (IsKeyDown(KEY_RIGHT)) {
        if (isRightArrowHeld) {
            const double timeHeld = currentTime - arrowHoldStartTime;
            if (timeHeld > ARROW_DELAY) {
                if (currentTime - lastArrowTime >= ARROW_REPEAT) {
                    if (cursorPosition < inputBuffer->length) {
                        cursorPosition++;
                        showCursor = true;
                        lastCursorBlink = currentTime;
                        lastArrowTime = currentTime;
                    }
                }
            }
        }
    }
    else {
        isRightArrowHeld = false;
    }


    // Handling regular character input
    int key = GetCharPressed(); // retrieves user input

    while (key > 0) {
        // Checks correct unicode range
        if (inputBuffer->length < MAX_LEN - 1 && key >= 32 && key <= 125) {
            insertCharacter(key);  // This will update cursorPosition
            showCursor = true;           // Show cursor after typing
            lastCursorBlink = currentTime;  // Reset blink timer
        }
        // Reads next key press
        key = GetCharPressed();
    }

    // Handling backspace (deleting a character)
    if (IsKeyPressed(KEY_BACKSPACE) && inputBuffer->length > 0) {
        if (cursorPosition > 0) {
            backspaceCharacter();
            showCursor = true;
            lastCursorBlink = currentTime;
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
                    if (cursorPosition > 0) {
                        backspaceCharacter();
                        showCursor = true;
                        lastCursorBlink = currentTime;
                        lastBackspaceTime = currentTime;
                    }
                }
            }
        }
        else {
            isBackspaceHeld = false;
        }
    }

    // Handle delete key
    if (IsKeyPressed(KEY_DELETE)) {
        if (cursorPosition < inputBuffer->length) {
            deleteCharacter();
            showCursor = true;
            lastCursorBlink = currentTime;
        }
    }

    // Handling enter (sending a message)
    if (IsKeyPressed(KEY_ENTER) && inputBuffer->length > 0) { //checking for empty messages
        inputBuffer->buffer[inputBuffer->length] = '\0'; // null terminator at the end of message buffer

        // Check for the /exit command
        if (strcmp(inputBuffer->buffer, "/exit") == 0) {
            cleanupAndExit();
        }

        // Sending message to server with error handling
        if (send(socketClient, inputBuffer->buffer, inputBuffer->length, 0) < 0) {
            addMessage("Error when sending message.", false);
        }

        // Clear input buffer and reset all input-related variables
        memset(inputBuffer->buffer, 0, MAX_LEN);
        inputBuffer->length = 0;
        cursorPosition = 0;  // Reset cursor to start position
        showCursor = true;   // Make cursor visible
        lastCursorBlink = GetTime();  // Reset blink timer
    }
}

//Method for drawing the input with the blinking cursor
void drawInputWithCursor() {
    // Check for null pointers
    if (!inputBuffer || !inputBuffer->buffer) {
        return;
    }

    // Draw input box and text
    DrawRectangleRec(inputBox, LIGHTGRAY);
    DrawRectangleLinesEx(inputBox, 1, DARKGRAY);
    DrawText(inputBuffer->buffer, (int)inputTextPosition.x, (int)inputTextPosition.y, FONT_SIZE, BLACK);

    // Draw cursor
    if (showCursor) {
        // Calculate cursor position
        float cursorX = inputTextPosition.x;
        if (cursorPosition > 0) {
            const char temp = inputBuffer->buffer[cursorPosition];
            inputBuffer->buffer[cursorPosition] = '\0';
            cursorX += (float) MeasureText(inputBuffer->buffer, FONT_SIZE);
            inputBuffer->buffer[cursorPosition] = temp;
        }

        DrawLineEx(
            (Vector2){cursorX, inputTextPosition.y + 2},
            (Vector2){cursorX, inputTextPosition.y + FONT_SIZE},
            4,
            BLACK
        );
    }
}

int main() {
    //User creation
    printf("Enter your name: ");
    fgets(user.name, sizeof(user.name), stdin); //retrieves name from stdin
    user.name[strcspn(user.name, "\n")] = '\0'; //adds the string end character to the name

    messages = malloc(MAX_MESSAGES * sizeof(ChatMessage*));
    if (!messages) {
        perror("Failed to allocate messages array");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < MAX_MESSAGES; i++) {
        messages[i] = malloc(sizeof(ChatMessage));
        messages[i]->text = malloc(MAX_MESSAGE_LENGTH * sizeof(char));
        if(!messages[i]->text) {
            perror("Failed to allocated messages' char array");
            exit(EXIT_FAILURE);
        }
    }

    inputBuffer = malloc(sizeof(InputBuffer));
    if (!inputBuffer) {
        perror("Failed to allocated inputBuffer's memory");
        exit(EXIT_FAILURE);
    }

    inputBuffer->buffer = malloc(MAX_LEN * sizeof(char));
    if (!inputBuffer->buffer) {
        perror("Failed to allocated inputBuffer's buffer memory");
        exit(EXIT_FAILURE);
    }

    inputBuffer->length = 0;
    inputBuffer->capacity = MAX_LEN;

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
        handleTextInput();

        // Handles scrolling
        if (GetMouseWheelMove() != 0) { //Detects mouse wheel movement
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
            const Color msgColor = messages[i]->color;

            const float textWidth = (float) MeasureText(messages[i]->text, FONT_SIZE);

            float x;

            if (messages[i]->isServerMessage) {
                // Center server messages
                x = SCREEN_WIDTH / 2.0 - textWidth/2;
            } else if (messages[i]->isOwn) {
                // Right align own messages
                x = SCREEN_WIDTH - textWidth - CHAT_MARGIN * 2;
            } else {
                // Left align other users' messages
                x = CHAT_MARGIN;
            }

            // Drawing message bubble
            DrawRectangle( (int)(x - 5), (int)(y - 25), (int)(textWidth + 10), 30, msgColor);

            // Drawing message content (within the bubble)
            DrawText(messages[i]->text, (int) x, (int) y - 20, 30, BLACK);

            y -= 35; // Moves up for next message
            if (y < -30) break;  // Stop if message isn't in view
        }
        // Release lock after displaying message
        pthread_mutex_unlock(&mutex);

        // Draw input box
        drawInputWithCursor();

        // End the drawing phase
        EndDrawing();
    }

    // Cleanup
    cleanupAndExit();

    return 0;
}