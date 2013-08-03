#ifndef ALGORITHM_REMOTE_PROTOCOL_H
#define ALGORITHM_REMOTE_PROTOCOL_H

// Begin processing an image.  Processing settings and image data follow.
#define CMD_START		1

#define CMD_GET_STATE		2

// Retrieve the processed image data.  This is only valid after image processing
// has finished; if received at any other time, an error will result.
#define CMD_GET_RESULT		3

// Abort the running processing, if any, and return to the initial state.
#define CMD_RESET		4

// Exit the worker thread.
#define CMD_SHUTDOWN		5

// A command was received correctly.  The command ID that was received will follow RESP_OK,
// followed by command-specific returned data, if any.
#define RESP_OK			1

// This may be returned as a response to any command, indicating that a fatal
// error has occurred.  The process must be shut down.
#define RESP_ERROR		2

// Something has changed; execute CMD_GET_STATE to find out what.
#define RESP_STATE_CHANGED	3

#endif
