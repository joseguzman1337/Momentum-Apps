# NFC PC Login

A Flipper Zero application that uses NFC cards to automatically type passwords on your computer via BadUSB. Perfect for quick desktop logins using NFC cards, tags or implants.

## Special Thanks

Special thanks to [**Equip**](https://github.com/equipter), [**tac0s**](https://github.com/dangerous-tac0s) and [**WillyJL**](https://github.com/willyjl/) for the help and suggestions while creating this app!

## Features

- **NFC Card Management**: Store multiple NFC cards with associated passwords
- **Automatic Password Typing**: Scan an NFC card to automatically type its password via USB HID
- **Secure Storage**: All card data is encrypted using the Flipper Zero's secure enclave (device unique key)
- **Card Selection**: Choose a specific card to use, or auto-match on scan
- **Persistent Selection**: Last selected card is remembered across app restarts
- **Visual Indicators**: Active card is marked with `*`, navigation cursor shows `>`
- **Keyboard Layout Support**: Full BadUSB keyboard layout support for international keyboards and symbols
- **Layout Selection**: Choose from available `.kl` layout files or cycle through them
- **Import Support**: Import NFC cards from `.nfc` files
- **Configurable Settings**: Adjust input delays, enter key behavior, and keyboard layout
- **Momentum Firmware Support**: Illegal character highlighting for text inputs (when using Momentum firmware)

## Security

- **Hardware Encryption**: Uses Flipper Zero's secure enclave (device unique key slot)
- **Device-Specific**: Encrypted data is tied to your specific device and cannot be read on other Flipper Zeros
- **Secure Key Storage**: Encryption keys never leave the secure enclave
- **AES-CBC Encryption**: All card data is encrypted using AES-CBC with PKCS7 padding

## Installation

1. Copy the `NFC Login` folder to your Flipper Zero's `applications_user` directory
2. Build using FBT (Flipper Build Tool):
   ```bash
   ./fbt launch APPSRC=nfc_login
   ```
3. Or use the pre-built `.fap` file if available

## Usage

### Adding a Card

1. Open the app from the Applications menu
2. Select **"Add NFC Card"**
3. Enter a name for the card
4. Hold your NFC card/tag to the Flipper Zero when prompted
5. Enter the password to associate with this card
6. The card is saved and encrypted automatically

### Using a Card for Login

1. Select **"Start Scan"** from the main menu
2. Hold your NFC card/tag to the Flipper Zero
3. The password will be automatically typed via USB HID
4. Press Back to stop scanning

### Selecting a Specific Card

1. Select **"List Cards"** from the main menu
2. Navigate to the card you want to use (use Up/Down to navigate)
3. Press **OK** to select it (you'll see a success notification and a `*` marker)
4. When scanning, only the selected card will trigger password entry
5. Press **OK** again on a different card to change selection
6. The selected card is remembered across app restarts

### Editing Cards

1. Go to **"List Cards"**
2. Navigate to the card you want to edit
3. **Hold OK** to open the edit menu
4. Choose what to edit:
   - **Name**: Change the card's display name
   - **Password**: Update the password
   - **UID**: Edit the UID (hex input) or scan a new card
   - **Delete**: Remove the card

### Importing Cards

1. Go to **"List Cards"**
2. Press **Right** (>) to open the file browser
3. Navigate to and select a `.nfc` file
4. Enter the password for the imported card
5. The card will be added to your list

### Settings

- **Append Enter**: Toggle whether to press Enter after typing the password
- **Input Delay**: Adjust the delay between key presses (10ms, 50ms, 100ms, or 200ms)
- **Keyboard Layout**: Select a BadUSB keyboard layout file (`.kl` format)
  - Press **OK** to browse and select a layout file
  - Use **Left/Right** to cycle through available layouts
  - Layouts are loaded from `/ext/badusb/assets/layouts/`
  - Default US layout is used if no layout is selected
  - Supports uppercase, lowercase, symbols, and special characters
- **Credits**: View credits and acknowledgments (navigate with Left/Right to view different pages)

## File Format

### Card Storage

Cards are stored in `/ext/apps_data/nfc_login/cards.enc` in encrypted format:
- **Encrypted Data**: AES-CBC encrypted card data with PKCS7 padding (no IV stored - IV is derived from device UID)

The plaintext format (before encryption) is:
```
name|uid_hex|password\n
```

Example:
```
Work Badge|04A1B2C3D4|MySecurePassword123
Home Key|1234567890|HomePassword
```

### Settings Storage

Settings are stored in `/ext/apps_data/nfc_login/settings.txt` in plaintext:
```
append_enter=1
input_delay=10
keyboard_layout=en-US.kl
active_card_index=2
```

- `append_enter`: 0 or 1 (whether to press Enter after password)
- `input_delay`: 10, 50, 100, or 200 (delay between key presses in milliseconds)
- `keyboard_layout`: Filename of the keyboard layout (e.g., "en-US.kl")
- `active_card_index`: Index of the last selected card (0-based, only saved if a card is selected)

## Technical Details

### Encryption

- **Algorithm**: AES-128-CBC
- **Key Source**: Secure Enclave device unique key slot (`FURI_HAL_CRYPTO_ENCLAVE_UNIQUE_KEY_SLOT`)
- **IV**: Derived from device UID (not stored in file - regenerated from device UID on each encryption/decryption)
- **Padding**: PKCS7 (padding value equals number of padding bytes)
- **Key Management**: Key is generated once per device and stored in secure enclave, never exposed to application code

### Keyboard Layouts

The app supports BadUSB keyboard layout files (`.kl` format) for international keyboard support:

- **Location**: `/ext/badusb/assets/layouts/`
- **Format**: Binary format - array of `uint16_t` keycodes with modifiers encoded in upper byte
- **Default**: US keyboard layout is built-in (supports A-Z, 0-9, and common symbols)
- **Modifiers**: Shift, Alt, and Ctrl are encoded in the upper byte of the keycode
- **Selection**: Layouts can be selected from the Settings menu or cycled with Left/Right keys

The layout system correctly handles:
- Uppercase letters (with Shift)
- Lowercase letters
- Numbers
- Symbols (with Shift when needed)
- Special characters

### Card Selection Persistence

- Selected card index is saved to `/ext/apps_data/nfc_login/settings.txt`
- On app restart, the last selected card is automatically restored
- If the saved card no longer exists (was deleted), the selection is cleared
- Visual indicator (`*`) shows which card is currently active

### Requirements

- Flipper Zero with USB HID support
- NFC cards/tags (ISO14443-3A compatible)
- Computer with USB HID keyboard support

### API Usage

- `furi_hal_crypto_enclave_ensure_key()`: Ensures device unique key exists (generates if needed)
- `furi_hal_crypto_enclave_load_key()`: Loads key from secure enclave
- `furi_hal_crypto_encrypt()` / `furi_hal_crypto_decrypt()`: Encrypts/decrypts data
- `furi_hal_crypto_enclave_unload_key()`: Unloads key from AES engine

## Building

Requires Flipper Build Tool (FBT) and the Flipper Zero firmware source.

```bash
cd applications_user/NFC\ Login
../../fbt launch APPSRC=nfc_login
```

## License

This project is licensed under the GNU General Public License v3.0 (GPLv3).

See the [LICENSE](LICENSE) file for details.


