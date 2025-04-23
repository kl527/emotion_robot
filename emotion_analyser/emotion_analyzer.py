import asyncio
import serial
import serial.tools.list_ports
from hume import AsyncHumeClient
from hume.expression_measurement.stream import Config
from hume.expression_measurement.stream.socket_client import StreamConnectOptions
import cv2
import base64
import time

happiness = [
    "Admiration", "Adoration", "Aesthetic Appreciation", "Amusement",
    "Calmness", "Contentment", "Craving", "Determination", "Ecstasy",
    "Entrancement", "Excitement", "Interest", "Joy", "Love", "Nostalgia",
    "Pride", "Realization", "Relief", "Romance", "Satisfaction", "Triumph"
]

sadness = [
    "Awkwardness", "Boredom", "Contemplation", "Disappointment",
    "Distress", "Doubt", "Embarrassment", "Empathic Pain", "Pain",
    "Sadness", "Shame", "Sympathy", "Tiredness"
]

anger = [
    "Anger", "Contempt", "Disgust", "Envy", "Guilt"
]

surprise = [
    "Surprise (positive)", "Surprise (negative)", "Confusion"
]

fear = [
    "Anxiety", "Awe", "Fear", "Horror"
]

def categorize_emotion(emotion_name):
    if emotion_name in happiness:
        return "Happiness"
    elif emotion_name in sadness:
        return "Sadness"
    elif emotion_name in anger:
        return "Anger"
    elif emotion_name in surprise:
        return "Surprise"
    elif emotion_name in fear:
        return "Fear"
    return "Neutral"

def write_to_arduino(emotion_category):
    try:
        arduino.write(bytes(emotion_category + '\n', 'utf-8'))
        time.sleep(0.05) 
        response = arduino.readline()
        return response
    except Exception as e:
        print(f"Error communicating with Arduino: {str(e)}")
        return None

def find_arduino_port():
    """
    Find the port the Arduino is connected to by listing all available ports
    and attempting to connect to each one that looks promising
    """
    ports = list(serial.tools.list_ports.comports())
    arduino_ports = []
    
    for port in ports:
        print(f"Found port: {port.device} - {port.description}")
        # Common identifiers for Arduino boards
        if "Arduino" in port.description or "CH340" in port.description or "USB Serial" in port.description:
            arduino_ports.append(port.device)
    
    # If we found obvious Arduino ports, try those first
    if arduino_ports:
        print(f"Found potential Arduino ports: {arduino_ports}")
        for port in arduino_ports:
            try:
                ser = serial.Serial(port=port, baudrate=9600, timeout=2)
                time.sleep(2)  # Allow Arduino to reset after connection
                
                # Try to read a line, Arduino should send something on startup
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                print(f"Response from {port}: {line}")
                
                # If we got a response that looks like our Arduino
                if "eyebrow" in line.lower() or "emotion" in line.lower() or "ready" in line.lower():
                    print(f"Arduino found on port {port}")
                    return ser
                
                ser.close()
            except Exception as e:
                print(f"Error testing port {port}: {str(e)}")
    
    # If we didn't find our Arduino with the above method, try all ports
    print("No Arduino found with auto-detection, trying all ports...")
    for port in [p.device for p in ports]:
        try:
            ser = serial.Serial(port=port, baudrate=9600, timeout=2)
            time.sleep(2)
            print(f"Trying port {port}...")
            
            # Send a test message
            ser.write(b"test\n")
            time.sleep(0.1)
            
            # Try to read response
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"Got response from {port}: {line}")
                return ser
            
            ser.close()
        except Exception as e:
            print(f"Error testing port {port}: {str(e)}")
    
    # If we get here, we couldn't find the Arduino
    raise Exception("Could not find Arduino on any port. Is it connected?")

async def main():
    global arduino
    try:
        print("Searching for Arduino...")
        arduino = find_arduino_port()
        print("Successfully connected to Arduino")
    except Exception as e:
        print(f"Failed to connect to Arduino: {str(e)}")
        # Fallback to a specific port if auto-detection fails
        port = input("Enter Arduino port manually (e.g. COM4, /dev/ttyUSB0): ")
        try:
            arduino = serial.Serial(port=port, baudrate=9600, timeout=.1)
            print(f"Connected to Arduino on {port}")
        except Exception as e:
            print(f"Failed to connect: {str(e)}")
            return

    client = AsyncHumeClient(api_key="jXvKPYTvVWO3XqqzGgi6WjoSmstGguywD3Mtvekscq67YU1R")

    model_config = Config(face={})
    stream_options = StreamConnectOptions(config=model_config)

    cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        print("Error: Could not open camera")
        return

    previous_category = None

    try:
        async with client.expression_measurement.stream.connect(options=stream_options) as socket:
            while True:
                ret, frame = cap.read()
                if not ret:
                    print("Error: Could not read frame")
                    break

                _, buffer = cv2.imencode('.jpg', frame)
                base64_image = base64.b64encode(buffer).decode('utf-8')

                result = await socket.send_file(base64_image)
                
                if result.face and result.face.predictions:
                    for prediction in result.face.predictions:
                        emotions = prediction.emotions
                        if emotions:
                            top_emotion = max(emotions, key=lambda x: x.score)
                            current_category = categorize_emotion(top_emotion.name)
                            print(f"Top emotion: {top_emotion.name} ({current_category}) - Score: {top_emotion.score:.2f}")
                            
                            if current_category != previous_category:
                                print(f"Emotion category changed to: {current_category}")
                                response = write_to_arduino(current_category)
                                if response:
                                    print(f"Arduino response: {response.decode('utf-8').strip()}")
                                previous_category = current_category

                await asyncio.sleep(2)

    except Exception as e:
        print(f"Error: {str(e)}")
    finally:
        cap.release()
        arduino.close()

if __name__ == "__main__":
    asyncio.run(main())