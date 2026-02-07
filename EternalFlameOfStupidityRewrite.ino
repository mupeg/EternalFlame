#include <FastLED.h>
#include <Bounce2.h> 

// ============================================================================
// 1. PIN DEFINITIONS
// ============================================================================
#define PIN_BUTTON       3    
#define PIN_POOFER_MAIN  2    
#define PIN_POOFER_AUX1  4    
#define PIN_POOFER_AUX2  5    
#define PIN_POOFER_AUX3  6    
#define PIN_FIRE_DATA    7    
#define PIN_FUSE_DATA    8    

Bounce debouncer = Bounce();

// ============================================================================
// 2. HARDWARE GEOMETRY
// ============================================================================
const int tube1H = 43;
const int tube2H = 43;
const int tube3H = 20;

const int segLens[] = { tube1H, tube2H, tube3H };
const int numFireLED = tube1H + tube2H + tube3H;
const int NUM_PHYSICAL_FIRE = (tube1H * 3) + (tube2H * 3) + (tube3H * 3);
const int numFuseLED = 80;

// ============================================================================
// 3. GAMEPLAY & ODDS
// ============================================================================
//100% winrate, no cooldown, all poofers guaranteed
//float winPercentage = 100.0;// 30.0;
//unsigned long minTimeTillNextWinMs = 1000;// 30000;
//int pooferAux1Chance = 100.0;
//int pooferAux2Chance = 100.0;
//int pooferAux3Chance = 0;

float winPercentage = 40.0;// 30.0;
unsigned long minTimeTillNextWinMs = 90000;// 30000;
int pooferAux1Chance = 50.0;
int pooferAux2Chance = 50.0;
int pooferAux3Chance = 0;

// ============================================================================
// 4. TIMING
// ============================================================================
unsigned long targetFuseTimeMs = 5000;
unsigned long pooferTime = 1500;        // Duration of EACH step (Visuals & Valves)
unsigned long pooferNextWait = 750;     // Delay between steps
float lossSpeedMult = 1.0;
float returnSpeedMult = 1.75;

// ============================================================================
// 5. INTERNAL STATE
// ============================================================================
CRGB fire[NUM_PHYSICAL_FIRE];
CRGB fuse[numFuseLED];
byte heat[numFireLED];
float winSpeed, lossSpeed, returnSpeed;

enum GamePhase { IDLE, FUSE_BURN, POOFING };
GamePhase currentPhase = IDLE;

bool isWinner = false, isHighIntensity = false, isReturning = false;
float fusePos = -1.0;
int lossPoint = 0;
unsigned long lastWinMs = 0;

// WAVE SEQUENCER STATE
// Index: 0=Visuals, 1=Main, 2=Aux1, 3=Aux2, 4=Aux3
unsigned long stopTimes[5];
int currentStep = 0;
unsigned long nextStepTime = 0;

// FRAME LIMITER
unsigned long lastFrameMs = 0;
const int FRAME_DELAY = 16;

void setup() {
    // 1. Safety Clamp
    int safePins[] = { PIN_POOFER_MAIN, PIN_POOFER_AUX1, PIN_POOFER_AUX2, PIN_POOFER_AUX3 };
    for (int p : safePins) { digitalWrite(p, LOW); pinMode(p, OUTPUT); digitalWrite(p, LOW); }

    // 2. Button Setup
    debouncer.attach(PIN_BUTTON, INPUT_PULLUP);
    debouncer.interval(5);

    // 3. Speed Calculations
    float totalFrames = (float)targetFuseTimeMs / 16.667;
    winSpeed = (float)numFuseLED / totalFrames;
    lossSpeed = winSpeed * lossSpeedMult;
    returnSpeed = winSpeed * returnSpeedMult;

    // 4. LED Setup
    FastLED.addLeds<WS2812B, PIN_FIRE_DATA, GRB>(fire, NUM_PHYSICAL_FIRE);
    FastLED.addLeds<WS2812B, PIN_FUSE_DATA, RGB>(fuse, numFuseLED);
    FastLED.setBrightness(255);
}

void loop() {
    debouncer.update();
    handleButton();

    unsigned long now = millis();
    if (now - lastFrameMs >= FRAME_DELAY) {
        lastFrameMs = now;

        fireUpdate();

        switch (currentPhase) {
        case FUSE_BURN:
            if (fuseUpdate()) {
                if (isWinner) {
                    // Visuals start immediately, triggering the cascade
                    fill_solid(fuse, numFuseLED, CRGB::Black);
                    triggerPooferSequence();
                }
                else {
                    currentPhase = IDLE;
                }
            }
            break;

        case POOFING:
            updatePooferLogic();
            break;

        case IDLE:
            digitalWrite(PIN_POOFER_MAIN, LOW);
            break;
        }
        mapAndShow();
    }
}

void handleButton() {
    if (debouncer.fell()) {
        if (currentPhase != IDLE) return;

        int roll = random(0, 101);
        isWinner = (roll <= winPercentage);

        // Win Suppression (Cooldown Logic)
        unsigned long timeSinceWin = millis() - lastWinMs;
        if (isWinner && lastWinMs != 0 && timeSinceWin < minTimeTillNextWinMs) {
            isWinner = false; // Force Loss
        }

        lossPoint = isWinner ? numFuseLED : random(numFuseLED * 0.35, numFuseLED * 0.85);

        fusePos = -1.0;
        isReturning = false;
        isHighIntensity = false;
        currentPhase = FUSE_BURN;
    }
}

bool fuseUpdate() {
    fill_solid(fuse, numFuseLED, CRGB::Black);
    float currentSpeed = isWinner ? winSpeed : lossSpeed;

    if (!isReturning) {
        fusePos += currentSpeed;

        for (int i = 0; i < 5; i++) {
            int logicalIdx = (int)fusePos - i;
            if (logicalIdx >= 0 && logicalIdx < numFuseLED) {
                int physicalIdx = (numFuseLED - 1) - logicalIdx;
                if (i == 0)      fuse[physicalIdx] = CRGB::Yellow;
                else if (i == 1) fuse[physicalIdx] = CRGB(255, 200, 0);
                else if (i == 2) fuse[physicalIdx] = CRGB::Orange;
                else if (i == 3) fuse[physicalIdx] = CRGB::Red;
                else             fuse[physicalIdx] = CRGB(100, 0, 0);
            }
        }

        if (isWinner && fusePos >= (float)numFuseLED) return true;

        if (!isWinner && fusePos >= (float)lossPoint) {
            isReturning = true;
        }
    }
    else {
        fusePos -= returnSpeed;
        for (int i = 0; i < 5; i++) {
            int logicalIdx = (int)fusePos + i;
            if (logicalIdx >= 0 && logicalIdx < numFuseLED) {
                int physicalIdx = (numFuseLED - 1) - logicalIdx;
                fuse[physicalIdx] = CRGB::DarkRed;
                fuse[physicalIdx].fadeToBlackBy(150 + (i * 20));
            }
        }
        if (fusePos <= -5.0) return true;
    }
    return false;
}

void fireUpdate() {
    int cooling = isHighIntensity ? 40 : 100;
    int sparking = isHighIntensity ? 220 : 60;

    if (!isHighIntensity && currentPhase == FUSE_BURN && isWinner) {
        sparking = 60 + (80 * (fusePos / numFuseLED));
    }

    // "Chimney Stack" Physics (One virtual tube)
    for (int i = 0; i < numFireLED; i++) {
        heat[i] = qsub8(heat[i], random8(0, ((cooling * 10) / numFireLED) + 2));
    }
    for (int k = numFireLED - 1; k >= 2; k--) {
        heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
    }
    if (random8() < sparking) {
        heat[random8(7)] = qadd8(heat[random8(7)], random8(160, 255));
    }
}

void mapAndShow() {
    int pOff = 0, lOff = 0;
    for (int s = 0; s < 3; s++) {
        int h = segLens[s];
        for (int i = 0; i < h; i++) {
            CRGB color = HeatColor(heat[lOff + i]);
            // Note: We use the stopTimes[0] (Visuals) to drive the White Flash
            bool visualActive = (stopTimes[0] > 0);
            unsigned long visualStart = stopTimes[0] - pooferTime;

            // Ignition Flash logic: If Visuals Active, AND first 50ms, AND Bottom Tube
            if (visualActive && s == 0 && i < 3 && (millis() - visualStart < 50)) {
                color = CRGB::White;
            }
            fire[pOff + i] = color;
            fire[pOff + (h * 2) - 1 - i] = color;
            fire[pOff + (h * 2) + i] = color;
        }
        pOff += (h * 3); lOff += h;
    }
    FastLED.show();
}

void triggerPooferSequence() {
    // Reset all overlapping timers
    for (int i = 0; i < 5; i++) stopTimes[i] = 0;

    // STEP 0: Visuals (Animation Poof) - Starts Instantly
    isHighIntensity = true;
    stopTimes[0] = millis() + pooferTime;

    // Schedule Step 1 (Main Poofer)
    currentStep = 1;
    nextStepTime = millis() + pooferNextWait;

    currentPhase = POOFING;
}

void updatePooferLogic() {
    unsigned long now = millis();
    int pooferPins[] = { -1, PIN_POOFER_MAIN, PIN_POOFER_AUX1, PIN_POOFER_AUX2, PIN_POOFER_AUX3 };

    // --- 1. START NEW STEPS ---
    if (currentStep > 0 && currentStep <= 4 && now >= nextStepTime) {
        bool fire = false;

        if (currentStep == 1) {
            fire = true; // Main is guaranteed
        }
        else {
            int chance = (currentStep == 2) ? pooferAux1Chance : (currentStep == 3 ? pooferAux2Chance : pooferAux3Chance);
            int roll = random(0, 101);
            fire = (roll < chance);
        }

        if (fire) {
            digitalWrite(pooferPins[currentStep], HIGH);
            stopTimes[currentStep] = now + pooferTime;
            nextStepTime = now + pooferNextWait;
            currentStep++;
        }
        else {
            currentStep = 99; // Stop spawning
        }
    }

    // --- 2. STOP EXPIRED STEPS ---
    bool active = false;
    for (int i = 0; i < 5; i++) {
        if (stopTimes[i] > 0) {
            if (now >= stopTimes[i]) {
                if (i == 0) isHighIntensity = false; // Stop Visuals
                else digitalWrite(pooferPins[i], LOW); // Stop Valve
                stopTimes[i] = 0;
            }
            else {
                active = true; // Still running
            }
        }
    }

    // --- 3. CHECK COMPLETION ---
    if ((currentStep > 4 || currentStep == 99) && !active) {
        // Safety Force Close
        isHighIntensity = false;
        digitalWrite(PIN_POOFER_MAIN, LOW);
        digitalWrite(PIN_POOFER_AUX1, LOW);
        digitalWrite(PIN_POOFER_AUX2, LOW);
        digitalWrite(PIN_POOFER_AUX3, LOW);

        lastWinMs = now;
        currentPhase = IDLE;
    }
}
