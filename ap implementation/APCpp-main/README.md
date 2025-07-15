# APCpp
C++ Library for Clients interfacing with the [Archipelago Multi-Game Randomizer](https://archipelago.gg)

# Usage

## Initialization

Run one of the `AP_Init` functions as the first call to the library:
- `AP_Init(const char*, const char*, const char*, const char*)` with IP, Game Name, Slot Name and password (can be `""`)
- `AP_Init(const char*)` with the filename corresponding to a generated single player game.

Then, you must call the following functions (any order):
- `AP_SetItemClearCallback(void (*f_itemclr)())` with a callback that clears all item states
- `AP_SetItemRecvCallback(void (*f_itemrecv)(int,bool))` with a callback that adds the item with the given ID (first parameter) to the current state.
The secound parameter decides whether or not to notify the player
- `AP_SetLocationCheckedCallback(void (*f_locrecv)(int))` with a callback that marks the location with the given id (first parameter) as checked.

Optionally, for finer configuration:
- `AP_EnableQueueItemRecvMsgs(bool)` Enables or disables Item Messages for Items received for the current game. Alternative to using the game's native item reception handler, if present. Defaults to on.

Optionally, for DeathLink:
- `AP_SetDeathLinkSupported(bool)` Enables or disables DeathLink from the Library. Defaults to off. NOTE: If on, expects DeathLink data from Archipelago.
- `AP_SetDeathLinkRecvCallback(void (*f_deathrecv)())` Alternative to manual query. Optional callback to handle DeathLink.

Optionally, if slot data is required:
- `AP_RegisterSlotDataIntCallback(std::string, void (*f_slotdata)(int))` Add a callback that receives an int from slot data with the first parameter as its key.
- `AP_RegisterSlotDataMapIntIntCallback(std::string, void (*f_slotdata)(std::map<int,int>))` Add a callback that receives an int to int map from slot data with the first parameter as its key.

Finally, call `AP_Start()`

## Operation during runtime

When the player completes a check, call `AP_SendItem(int)` with the Item ID as the parameter.
When the player completes the game, call `AP_StoryComplete`.

### DeathLink
If DeathLink is supported, you have multiple ways of using it:
- Regularly call `AP_DeathLinkPending()` and check the return value. If true, kill the player and call `AP_DeathLinkClear()` (Preferably after a short time.
Faulty clients can send multiple deaths in quick succession. Ex.: Clear only after player has recovered from death).
- Handle death using a DeathLink callback (Registration described in [Initialization](#Initialization))

### Messages
Messages can be received from the Archipelago Server and this Library, such as Messages describing which item was sent, who was responsible for a Death received with DeathLink, etc.
To receive messages:
- Check if message is available using `AP_IsMessagePending()`
- Receive with `AP_GetLatestMessage()`. This returns an AP_Message struct with a type and preconfigured presentable text for this message. If you want the game client to have more details for the message (for example to create a custom text) any non-plaintext type message can be casted to an AP_`TYPE`Message with some type-specific struct members providing additional information for the given message.
- Clear the latest message with `AP_ClearLatestMessage()`.

# Building
Clone the Repo recursively!
## Linux
- Create a folder `build`
- `cd build`
- `cmake ..`
- `cmake --build .`
## Windows
- Create a folder `build`
- Enter the folder
- `cmake .. -DWIN32=1` (If on MinGW, also add `-DMINGW=1`)
- `cmake --build .`
