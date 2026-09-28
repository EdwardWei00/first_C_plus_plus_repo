// PROJECT: Smart Door Digital Keypad Acces System
// PROGRAMMING LANGUAGE: Modern C++20
// COMPILER COMPATIBILITY: g++ version 16.2.0 (MSYS2 project)
//  ----------------------------------------------------------------------------
#include <iostream>    //  No third-party libraries, only using standard C++ library
#include <string>       
#include <string_view>  
#include <chrono>       
#include <thread>       
#include <array>        
#include <cctype>       
#include <cstdint>      
#include <iomanip>      
#include <cassert>      

// ----------------------------------------------------------------------------
// ENUMERATION: PinValidationResult
// Represents all possible outcomes when checking the format of a PIN.
// ----------------------------------------------------------------------------
enum class PinValidationResult {
    Valid,                  // Ensure the input format is correct, should be exactly 4 digits (0-9)
    InvalidLength,          // Input string length is not equal to 4
    NonNumericDigits,       // Input contains letters, spaces, or symbols
    ConfirmationMismatch    // Occurs when the second confirmation PIN does not match the first
};

// ----------------------------------------------------------------------------
// ENUMERATION: UnlockResult
// Represents the outcome of an unlock attempt.
// ----------------------------------------------------------------------------
enum class UnlockResult {
    Success,                // PIN matched, door will be unlocked
    IncorrectPin,           // PIN was incorrect, but the failed attempt limit not reached
    LockoutTriggered        // Systems entered lockout mode, if there's multiple of 3 failures
};

// ----------------------------------------------------------------------------
// CLASS: SmartDoorModel
// Manages door status, failure counters, and lockout calculations.
// ----------------------------------------------------------------------------
class SmartDoorModel {
public:
    // Explicit constructor to prevent unintended conversions
    explicit SmartDoorModel(std::string defaultPin = "1234");

    // Inspects if a given string match to the 4-digit PIN
    [[nodiscard]] static PinValidationResult validatePinFormat(std::string_view candidatePin);

    // Evaluates an entered PIN against the stored PIN
    [[nodiscard]] UnlockResult attemptUnlock(std::string_view enteredPin);

    // Validates and updates the stored PIN credential
    [[nodiscard]] PinValidationResult updatePin(std::string_view newPin, std::string_view confirmPin);

    // Checks whether the keypad is currently lockout
    [[nodiscard]] bool isLockedOut() const;

    // Calculates remaining seconds for the lockout penalty
    [[nodiscard]] int64_t getRemainingLockoutSeconds() const;

    // Returns current consecutive failed attempt count
    [[nodiscard]] uint32_t getFailedAttempts() const;

    // Returns current lockout count
    [[nodiscard]] uint32_t getLockoutTier() const;

    // Searches whether the physical door bolt is unlocked
    [[nodiscard]] bool isDoorUnlocked() const;

    void lockDoor();

private:
    // Determines the penalty duration based on the failure counters
    [[nodiscard]] int64_t calculateLockoutDuration(uint32_t tierIndex) const;

    std::string currentPin_;            // Current default 4-digit PIN "1234"
    uint32_t consecutiveFailures_;      // Tracks consecutive failed unlock attempts
    uint32_t lockoutTierCount_;         // Tracks how many times lockouts have triggered
    bool doorUnlockedState_;            // True if door is unlocked, false if locked

    // Timer tracking when the current lockout expires
    std::chrono::steady_clock::time_point lockoutEndTime_;

    // List of Penatly 60s, 300s, 900s, 1800s, and 3600s
    static constexpr std::array<int64_t, 5> LOCKOUT_DURATIONS_SECONDS = {
        60,     // (3 failures)  : 1 minute
        300,    // (6 failures)  : 5 minutes
        900,    // (9 failures)  : 15 minutes
        1800,   // (12 failures) : 30 minutes
        3600    // (15+ failures): 60 minutes
    };
};

// ----------------------------------------------------------------------------
// CLASS: KeypadConsoleView
// Responsible for ASCII graphics and terminal alerts.
// ----------------------------------------------------------------------------
class KeypadConsoleView {
public:
    // Clears the console display using standard ANSI terminal escape sequences
    static void clearScreen();

    // Provide keypad interface and status dashboard
    static void renderKeypad(bool isDoorUnlocked, uint32_t failures);

    // Displays an access granted banner
    static void showUnlockSuccess();

    // Displays an access denied warning banner with remaining attempts before lockout
    static void showIncorrectPinAlert(uint32_t currentFailures, uint32_t attemptsRemaining);

    // Displays a security lockout alert banner
    static void showLockoutAlert(int64_t totalSeconds);

    // Provide countdown seconds on the terminal
    static void renderCountdownTick(int64_t secondsLeft);

    // Displays the PIN modification dialog header
    static void showPinChangeHeader();
};

// IMPLEMENTATION: SmartDoorModel
// ----------------------------------------------------------------------------
// CONSTRUCTOR: Initializes door status and default PIN
// ----------------------------------------------------------------------------
SmartDoorModel::SmartDoorModel(std::string defaultPin)
    : currentPin_(std::move(defaultPin)),               // Move initial PIN into storage
      consecutiveFailures_(0),                          // Zero initial failures
      lockoutTierCount_(0),                             // Starts at 0 failure counter
      doorUnlockedState_(false),                        // Door starts locked by default
      lockoutEndTime_(std::chrono::steady_clock::now()) // Initialize timer
{

}
// ----------------------------------------------------------------------------
// METHOD: validatePinFormat
// Verifies user PIN has length of 4 and contains only digits 0-9
// ----------------------------------------------------------------------------
PinValidationResult SmartDoorModel::validatePinFormat(std::string_view candidatePin) {
    // Condition 1: Must be exactly 4 characters
    if (candidatePin.length() != 4) {
        return PinValidationResult::InvalidLength;
    }

    // Condition 2: Every single character must be an integer from '0' to '9'
    for (char character : candidatePin) {
        // std::isdigit checks if character is between '0' and '9'
        if (!std::isdigit(static_cast<unsigned char>(character))) {
            return PinValidationResult::NonNumericDigits;
        }
    }
    // Both conditions are valid
    return PinValidationResult::Valid;
}
// ----------------------------------------------------------------------------
// METHOD: attemptUnlock
// Verifies user PIN, tracks consecutive failures, and activates lockouts
// ----------------------------------------------------------------------------
UnlockResult SmartDoorModel::attemptUnlock(std::string_view enteredPin) {
    // Check if the keypad is currently locked out before processing any input
    if (isLockedOut()) {
        return UnlockResult::LockoutTriggered;
    }

    // Case 1: Entered PIN matching stored PIN
    if (enteredPin == currentPin_) {
        doorUnlockedState_ = true;       // Unlocks door
        consecutiveFailures_ = 0;        // Reset consecutive failures on success
        return UnlockResult::Success;    // Notify user of success
    }

    // Case 2: PIN is incorrect, then failed attempt count increases
    consecutiveFailures_++;

    // Check if consecutive failures reached a multiple of 3
    if (consecutiveFailures_ % 3 == 0) {
        // Look up penalty duration based on how many lockouts have occurred
        int64_t durationSeconds = calculateLockoutDuration(lockoutTierCount_);

        // Calculate lockout expiration timestamp
        lockoutEndTime_ = std::chrono::steady_clock::now() + std::chrono::seconds(durationSeconds);

        // Advance to the next penalty for any subsequent failed attempt count
        lockoutTierCount_++;

        return UnlockResult::LockoutTriggered;
    }

    // Wrong PIN, but has not reached a multiple of 3 yet
    return UnlockResult::IncorrectPin;
}

// ----------------------------------------------------------------------------
// METHOD: updatePin
// Validates formatting and confirmation match before saving new PIN
// ----------------------------------------------------------------------------
PinValidationResult SmartDoorModel::updatePin(std::string_view newPin, std::string_view confirmPin) {
    // Step 1: Validate formatting of the new PIN
    PinValidationResult formatResult = validatePinFormat(newPin);
    if (formatResult != PinValidationResult::Valid) {
        return formatResult; // Return format error such as invalid length or non numeric digits
    }

    // Step 2: Validate that the confirmation PIN matches new PIN
    if (newPin != confirmPin) {
        return PinValidationResult::ConfirmationMismatch;
    }

    // Step 3: All validations are passed, then update the stored PIN storage
    currentPin_ = std::string(newPin);
    return PinValidationResult::Valid;
}

// ----------------------------------------------------------------------------
// METHOD: isLockedOut
// Checks whether current time is before lockout expiration time
// ----------------------------------------------------------------------------
bool SmartDoorModel::isLockedOut() const {
    return std::chrono::steady_clock::now() < lockoutEndTime_;
}

// ----------------------------------------------------------------------------
// METHOD: getRemainingLockoutSeconds
// Returns the number of seconds remaining in the current lockout
// ----------------------------------------------------------------------------
int64_t SmartDoorModel::getRemainingLockoutSeconds() const {
    auto now = std::chrono::steady_clock::now();

    // If timer has already elapsed, return 0
    if (now >= lockoutEndTime_) {
        return 0;
    }

    // Convert time duration into seconds, then round up frational seconds by adding 1.
    auto durationLeft = std::chrono::duration_cast<std::chrono::seconds>(lockoutEndTime_ - now);
    return durationLeft.count() + 1;
}

// ----------------------------------------------------------------------------
// METHOD: calculateLockoutDuration
// Returns the lockout duration in seconds corresponding to the penalty
// ----------------------------------------------------------------------------
int64_t SmartDoorModel::calculateLockoutDuration(uint32_t tierIndex) const {
    // If penalty exceeds available penalty timer duration, then it only capped at 3600s / 60mins
    if (tierIndex >= LOCKOUT_DURATIONS_SECONDS.size()) {
        return LOCKOUT_DURATIONS_SECONDS.back();
    }
    return LOCKOUT_DURATIONS_SECONDS[tierIndex];
}

uint32_t SmartDoorModel::getFailedAttempts() const {
    return consecutiveFailures_;
}

uint32_t SmartDoorModel::getLockoutTier() const {
    return lockoutTierCount_;
}

bool SmartDoorModel::isDoorUnlocked() const {
    return doorUnlockedState_;
}

void SmartDoorModel::lockDoor() {
    doorUnlockedState_ = false;
}


// IMPLEMENTATION: KeypadConsoleView
// ----------------------------------------------------------------------------
// FUNCTION: clearScreen
// Sends ANSI terminal escape sequence to clear screen and reset cursor
// ----------------------------------------------------------------------------
void KeypadConsoleView::clearScreen() {
    // "\033[2J" clears entire screen whereas"\033[1;1H" moves cursor to row 1, column 1
    std::cout << "\033[2J\033[1;1H" << std::flush;
}

// ----------------------------------------------------------------------------
// FUNCTION: renderKeypad
// Build the ASCII keypad hardware simulation and active system status code
// ----------------------------------------------------------------------------
void KeypadConsoleView::renderKeypad(bool isDoorUnlocked, uint32_t failures) {
    std::cout << "===========================================================\n";
    std::cout << "            SMART DOOR DIGITAL KEYPAD ACCESS SYSTEM          \n";
    std::cout << "===========================================================\n";

    // Display door lock bolt status
    std::cout << " Door Status   : " << (isDoorUnlocked ? "[ UNLOCKED / OPEN ]" : "[ LOCKED ]") << "\n";

    // Display remaining attempts before triggering lockout
    uint32_t remainingTries = 3 - (failures % 3);
    std::cout << " Attempts Left : " << remainingTries << " before lockout\n";
    std::cout << "-----------------------------------------------------------\n";

    // Draw the ASCII keypad buttons
    std::cout << "                        +---+---+---+\n";
    std::cout << "                        | 1 | 2 | 3 |\n";
    std::cout << "                        +---+---+---+\n";
    std::cout << "                        | 4 | 5 | 6 |\n";
    std::cout << "                        +---+---+---+\n";
    std::cout << "                        | 7 | 8 | 9 |\n";
    std::cout << "                        +---+---+---+\n";
    std::cout << "                        | * | 0 | # |\n";
    std::cout << "                        +---+---+---+\n";
    std::cout << "-----------------------------------------------------------\n";
    std::cout << " [Commands]:                                        \n";
    std::cout << "   * Enter 4-Digit PIN to Unlock (Default: 1234)   \n";
    std::cout << "   * Enter 'Secret PIN' to Access PIN Update Mode ('0#0#')\n";
    std::cout << "   * Enter 'exit' to shut down simulation          \n";
    std::cout << "===========================================================\n";
}

// ----------------------------------------------------------------------------
// FUNCTION: showUnlockSuccess
// Prints access granted message
// ----------------------------------------------------------------------------
void KeypadConsoleView::showUnlockSuccess() {
    std::cout << "\n>>> [ACCESS GRANTED]: Welcome! The door is UNLOCKED.\n";
}

// ----------------------------------------------------------------------------
// FUNCTION: showIncorrectPinAlert
// Prints incorrect PIN alert with attempts left before lockout
// ----------------------------------------------------------------------------
void KeypadConsoleView::showIncorrectPinAlert(uint32_t currentFailures, uint32_t attemptsRemaining) {
    std::cout << "\n>>> [ACCESS DENIED]: Incorrect PIN entered!\n";
    std::cout << "    Consecutive failures: " << currentFailures << "\n";
    std::cout << "    WARNING: " << attemptsRemaining << " attempts remaining before screen lockout.\n";
}

// ----------------------------------------------------------------------------
// FUNCTION: showLockoutAlert
// Prints security breach banner when lockout is triggered
// ----------------------------------------------------------------------------
void KeypadConsoleView::showLockoutAlert(int64_t totalSeconds) {
    std::cout << "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
    std::cout << "  SECURITY BREACH DETECTED: 3 Consecutive Failures! \n";
    std::cout << "  Keypad is now LOCKED for " << totalSeconds << " seconds.\n";
    std::cout << "  Please wait for the lockout countdown to expire.  \n";
    std::cout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
}

// ----------------------------------------------------------------------------
// FUNCTION: renderCountdownTick
// Updates the remaining seconds using return '\r'
// ----------------------------------------------------------------------------
void KeypadConsoleView::renderCountdownTick(int64_t secondsLeft) {
    std::cout << "\r[*] System frozen. Unlocking in: "
              << std::setw(4) << secondsLeft << "s remaining... " << std::flush;
}

// ----------------------------------------------------------------------------
// FUNCTION: showPinChangeHeader
// Prints banner for PIN update mode
// ----------------------------------------------------------------------------
void KeypadConsoleView::showPinChangeHeader() {
    std::cout << "\n====================================================\n";
    std::cout << "          PIN MANAGEMENT CONFIGURATION MODE         \n";
    std::cout << "====================================================\n";
    std::cout << " You entered secret PIN '0#0#'.                 \n";
    std::cout << " Requirement: PIN must be exactly 4 digits (0-9).   \n";
    std::cout << "====================================================\n";
}

// ============================================================================
// FUNCTION: runAutomatedVerificationTests
// Self-testing unit assertions to verify logic before launching interactive UI
// ============================================================================
void runAutomatedVerificationTests() {
    std::cout << "[SYSTEM AUDIT]: Running automated logic tests..." << std::endl;

    // Test 1: Default PIN setup
    SmartDoorModel testDoor("1234");
    assert(!testDoor.isDoorUnlocked()); // Door must start in locked state

    // Test 2: PIN formatting validator checks
    assert(SmartDoorModel::validatePinFormat("1234") == PinValidationResult::Valid);
    assert(SmartDoorModel::validatePinFormat("123") == PinValidationResult::InvalidLength);
    assert(SmartDoorModel::validatePinFormat("12345") == PinValidationResult::InvalidLength);
    assert(SmartDoorModel::validatePinFormat("12a4") == PinValidationResult::NonNumericDigits);
    assert(SmartDoorModel::validatePinFormat("####") == PinValidationResult::NonNumericDigits);

    // Test 3: Failure escalation calculation
    // Attempt 1: Wrong PIN
    UnlockResult res1 = testDoor.attemptUnlock("0000");
    assert(res1 == UnlockResult::IncorrectPin);
    assert(testDoor.getFailedAttempts() == 1);

    // Attempt 2: Wrong PIN
    UnlockResult res2 = testDoor.attemptUnlock("0000");
    assert(res2 == UnlockResult::IncorrectPin);
    assert(testDoor.getFailedAttempts() == 2);

    // Attempt 3: Triggers first 60 seconds lockout
    UnlockResult res3 = testDoor.attemptUnlock("0000");
    assert(res3 == UnlockResult::LockoutTriggered);
    assert(testDoor.isLockedOut());
    assert(testDoor.getLockoutTier() == 1);
    assert(testDoor.getRemainingLockoutSeconds() > 0);

    // Test 4: PIN change validation
    SmartDoorModel updateDoor("1234");
    // Mismatched confirmation
    assert(updateDoor.updatePin("5678", "9999") == PinValidationResult::ConfirmationMismatch);
    // Non-numeric PIN
    assert(updateDoor.updatePin("56ab", "56ab") == PinValidationResult::NonNumericDigits);
    // Successful update
    assert(updateDoor.updatePin("5678", "5678") == PinValidationResult::Valid);
    // Old PIN no longer works
    assert(updateDoor.attemptUnlock("1234") == UnlockResult::IncorrectPin);
    // New PIN works
    assert(updateDoor.attemptUnlock("5678") == UnlockResult::Success);
    assert(updateDoor.isDoorUnlocked());

    std::cout << "[SYSTEM AUDIT]: All unit assertions PASSED successfully!\n" << std::endl;
}

// ============================================================================
// FUNCTION: handleLockoutCountdown
// Controls the terminal countdown loop while the keypad is frozen
// ============================================================================
void handleLockoutCountdown(const SmartDoorModel& door) {
    int64_t totalSeconds = door.getRemainingLockoutSeconds();
    KeypadConsoleView::showLockoutAlert(totalSeconds);

    // Loop until the lockout timer expires
    while (door.isLockedOut()) {
        int64_t remaining = door.getRemainingLockoutSeconds();
        KeypadConsoleView::renderCountdownTick(remaining);

        // Sleep for 1 second between updates
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\n\n>>> [KEYPAD RESTORED]: Lockout expired. Keypad is ready for use.\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

// ============================================================================
// FUNCTION: handlePinUpdateWorkflow
// Implements the '0#0#' for changing new PIN and confirm it
// ============================================================================
void handlePinUpdateWorkflow(SmartDoorModel& door) {
    KeypadConsoleView::clearScreen();
    KeypadConsoleView::showPinChangeHeader();

    std::string newPin;
    std::string confirmPin;

    std::cout << "Step 1: Enter new 4-digit PIN: ";
    std::getline(std::cin, newPin);

    std::cout << "Step 2: Re-enter new 4-digit PIN to confirm: ";
    std::getline(std::cin, confirmPin);

    // Execute update validation in model
    PinValidationResult result = door.updatePin(newPin, confirmPin);

    std::cout << "\n----------------------------------------------------\n";
    switch (result) {
        case PinValidationResult::Valid:
            std::cout << "[SUCCESS]: PIN successfully changed to: " << newPin << "\n";
            std::cout << "You can now use this new PIN to unlock the door.\n";
            break;

        case PinValidationResult::InvalidLength:
            std::cout << "[ERROR]: PIN must be EXACTLY 4 digits long. Update cancelled.\n";
            break;

        case PinValidationResult::NonNumericDigits:
            std::cout << "[ERROR]: PIN must contain only NUMBERS (0-9). Update cancelled.\n";
            break;

        case PinValidationResult::ConfirmationMismatch:
            std::cout << "[ERROR]: Confirmation PIN did not match first entry. Update cancelled.\n";
            break;
    }
    std::cout << "----------------------------------------------------\n";
    std::cout << "Press ENTER to return to the keypad...";
    std::string dummy;
    std::getline(std::cin, dummy);
}

// ----------------------------------------------------------------------------
// MAIN APPLICATION ENTRY POINT
// ----------------------------------------------------------------------------
int main() {
    // Step 1: Run automated unit test assertions first to ensure logic correctness
    runAutomatedVerificationTests();

    std::cout << "Press ENTER to launch the Interactive Keypad Prototype...";
    std::string enterKey;
    std::getline(std::cin, enterKey);

    // Step 2: Initialize Smart Door model with default PIN "1234"
    SmartDoorModel door("1234");

    bool running = true; // Application loop flag

    // Step 3: Interactive Visual Keypad Loop
    while (running) {
        // Clear terminal screen and provide the keypad UI
        KeypadConsoleView::clearScreen();
        KeypadConsoleView::renderKeypad(
            door.isDoorUnlocked(),
            door.getFailedAttempts()
        );

        // Prompt user for input
        std::cout << "\nEnter Keypad Input > ";
        std::string input;
        if (!std::getline(std::cin, input)) {
            break; // Handle unexpected inputfailure
        }

        // Command 1: Exit application 
        if (input == "exit" || input == "EXIT") {
            std::cout << "\nShutting down Smart Door Keypad System. Goodbye!\n";
            running = false;
            continue;
        }

        // Command 2: PIN Update Command "0#0#" 
        if (input == "0#0#") {
            handlePinUpdateWorkflow(door);
            continue;
        }

        // Command 3: Standard PIN Unlock Entry 
        UnlockResult result = door.attemptUnlock(input);

        if (result == UnlockResult::Success) {
            KeypadConsoleView::showUnlockSuccess();
            std::cout << "\nPress ENTER to continue (door will automatically relock)...";
            std::getline(std::cin, enterKey);
            // Automatically relock door after user acknowledges access
            door.lockDoor();
        }
        else if (result == UnlockResult::IncorrectPin) {
            uint32_t triesLeft = 3 - (door.getFailedAttempts() % 3);
            KeypadConsoleView::showIncorrectPinAlert(door.getFailedAttempts(), triesLeft);
            std::cout << "\nPress ENTER to try again...";
            std::getline(std::cin, enterKey);
        }
        else if (result == UnlockResult::LockoutTriggered) {
            // Lockout triggered then start live countdown freeze
            handleLockoutCountdown(door);        }
    }

    return 0;
}