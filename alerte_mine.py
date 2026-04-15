import cv2 
import requests 
import base64 
import time 
import serial # NOUVEAU: Permet de communiquer avec l'Arduino en USB

# --- TA CONFIGURATION ROBOFLOW --- 
API_KEY = "wtHYyUNNA54pG1jHaga8" 
PROJECT_ID = "landmine-k5eze-pvidr" 
VERSION = "1" 
URL = f"https://detect.roboflow.com/{PROJECT_ID}/{VERSION}?api_key={API_KEY}" 

# --- CONFIGURATION ARDUINO ---
# Remplace 'COM3' par le port de ton Arduino (ex: '/dev/ttyACM0' sur Linux/Mac ou 'COM5' sur Windows)
PORT_ARDUINO = 'COM3' 
BAUD_RATE = 115200

try:
    # Initialisation de la connexion avec l'Arduino
    arduino = serial.Serial(PORT_ARDUINO, BAUD_RATE, timeout=1)
    print(f"✅ Connecté à l'Arduino sur {PORT_ARDUINO}")
    time.sleep(2) # Laisse le temps à l'Arduino de redémarrer après la connexion série
except Exception as e:
    print(f"❌ Erreur de connexion à l'Arduino : {e}")
    print("Vérifie le port COM. Le script continuera sans envoyer à l'Arduino.")
    arduino = None

# === RÉGLAGE DE LA SENSIBILITÉ === 
SEUIL_CONFIANCE = 0.60  
# ================================= 

print("Système d'alerte activé... (Appuie sur Ctrl+C pour arrêter)") 
cap = cv2.VideoCapture(0) 
time.sleep(2) # Pour s'assurer que la caméra est prête 

try: 
    while True: 
        ret, frame = cap.read() 
        if not ret: 
            print("Erreur : Impossible d'accéder à la webcam.") 
            break 
  
        # 1. Préparation de l'image (Compression JPG + Base64) 
        retval, buffer = cv2.imencode('.jpg', frame) 
        img_str = base64.b64encode(buffer).decode('ascii') 
  
        # 2. Envoi de l'image à l'API Roboflow 
        try: 
            response = requests.post( 
                URL,  
                data=img_str,  
                headers={"Content-Type": "application/x-www-form-urlencoded"}, 
                timeout=5 
            ) 
            data = response.json() 
  
            # 3. Analyse des prédictions 
            if "predictions" in data: 
                mine_trouvee = False 
                for pred in data["predictions"]: 
                    if pred["class"] == "landmines": 
                        confiance = pred["confidence"] 
                         
                        # On ne déclenche que si on dépasse le seuil 
                        if confiance >= SEUIL_CONFIANCE: 
                            pourcentage = int(confiance * 100) 
                            print(f"🚨 MINE DÉTECTÉE (Confiance: {pourcentage}%) 🚨") 
                            mine_trouvee = True 
                            
                            # NOUVEAU: Envoi du signal à l'Arduino !
                            if arduino and arduino.is_open:
                                arduino.write(b"DETECTED\n")
                                print("--> Signal 'DETECTED' envoyé à l'Arduino.")
                                
                                # On fait une pause un peu plus longue pour laisser l'Arduino
                                # envoyer son message LoRa sans le spammer
                                time.sleep(4) 
                 
                if not mine_trouvee: 
                    print("Analyse en cours... RAS", end="\r") 
  
        except Exception as e: 
            print(f"\nErreur réseau ou API : {e}") 
         
        # Pause pour ne pas saturer l'API Roboflow 
        time.sleep(1) 
  
except KeyboardInterrupt: 
    print("\nArrêt du système par l'utilisateur.") 
  
finally: 
    cap.release()
    if arduino and arduino.is_open:
        arduino.close()
    print("Webcam et Serial libérés. Au revoir !")