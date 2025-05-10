import asyncio
import serial
import serial.tools.list_ports
from hume import AsyncHumeClient
from hume.expression_measurement.stream import Config
from hume.expression_measurement.stream.socket_client import StreamConnectOptions
import cv2
import base64
import time
import ssl

# Ensure we use a secure HTTPS context for all Hume API requests
ssl._create_default_https_context = ssl.create_default_context

# -------------------------------------------------------------------
# Define fine-grained emotion lists for mapping into broad categories
# -------------------------------------------------------------------
SADNESS_EMOTIONS = [
    "Awkwardness", "Boredom", "Contemplation", "Disappointment",
    "Distress", "Doubt", "Embarrassment", "Empathic Pain", "Pain",
    "Sadness", "Shame", "Sympathy", "Tiredness"
]

ANGER_EMOTIONS = [
    "Anger", "Contempt", "Disgust", "Envy", "Guilt"
]

NEUTRAL_EMOTIONS = [
    "Calmness", "Contentment", "Realization"
]

SUPER_HAPPY_EMOTIONS = [
    "Craving", "Determination", "Ecstasy", "Entrancement",
    "Excitement", "Joy", "Pride", "Triumph"
]

SEMI_HAPPY_EMOTIONS = [
    "Admiration", "Adoration", "Aesthetic Appreciation", "Amusement",
    "Love", "Nostalgia", "Relief", "Romance", "Satisfaction"
]

FEAR_EMOTIONS = [
    "Anxiety", "Awe", "Fear", "Horror"
]

CURIOUS_EMOTIONS = [
    "Interest", "Surprise (positive)", "Surprise (negative)", "Confusion"
]


def categorize_hume_emotion(emotion_label: str) -> str:
    """
    Map a detailed Hume emotion name into one of our broad categories.
    """
    if emotion_label in SADNESS_EMOTIONS:
        return "Sadness"
    elif emotion_label in ANGER_EMOTIONS:
        return "Anger"
    elif emotion_label in SUPER_HAPPY_EMOTIONS:
        return "Super_Happy"
    elif emotion_label in SEMI_HAPPY_EMOTIONS:
        return "Semi_Happy"
    elif emotion_label in FEAR_EMOTIONS:
        return "Fear"
    elif emotion_label in CURIOUS_EMOTIONS:
        return "Curious"
    else:
        return "Neutral"


def send_message_to_arduino(emotion_category: str, face_position: str) -> bytes:
    """
    Send a formatted message to the Arduino over serial.
    Returns the Arduino's raw response bytes, or None on error.
    """
    try:
        message = f"{emotion_category},{face_position}\n"
        arduino_serial.write(message.encode('utf-8'))
        time.sleep(0.05)  # small delay to allow Arduino to process
        return arduino_serial.readline()
    except Exception as err:
        print(f"Error communicating with Arduino: {err}")
        return None


def connect_to_arduino(port: str = "/dev/cu.usbserial-021FEEA5",
                       baudrate: int = 9600,
                       timeout: float = 2.0) -> serial.Serial:
    """
    Establish a serial connection to the Arduino.
    Raises serial.SerialException if connection fails.
    """
    ser = serial.Serial(port=port, baudrate=baudrate, timeout=timeout)
    time.sleep(2)  # allow Arduino to reset after opening port
    print(f"Connected to Arduino on {port}")
    return ser


async def main():
    """
    Main async loop:
      - Connect to Arduino
      - Initialize Hume streaming client
      - Open camera and detect face + emotion
      - Relay events and emotion categories to Arduino
    """
    global arduino_serial
    face_in_view_before = False

    # Attempt to connect to Arduino, prompt if auto-detect fails
    try:
        print("Attempting to connect to Arduino...")
        arduino_serial = connect_to_arduino()
    except serial.SerialException as e:
        print(f"Auto-connect failed: {e}")
        manual_port = input("Enter Arduino port manually (e.g. COM4, /dev/ttyUSB0): ")
        try:
            arduino_serial = serial.Serial(port=manual_port, baudrate=9600, timeout=0.1)
            print(f"Manually connected to Arduino on {manual_port}")
        except Exception as e2:
            print(f"Manual connect failed: {e2}")
            return

    # Add you arduino api_key
    hume_client = AsyncHumeClient(api_key="your_own_api_key")
    model_config = Config(face={})
    stream_options = StreamConnectOptions(config=model_config)

    # Try opening the default camera, fallback across indices if needed
    current_camera_index = 0
    MAX_CAMERA_ATTEMPTS = 3
    video_capture = None
    for _ in range(MAX_CAMERA_ATTEMPTS):
        video_capture = cv2.VideoCapture(current_camera_index)
        if video_capture.isOpened():
            ret, test_frame = video_capture.read()
            if ret:
                print(f"Camera opened at index {current_camera_index}")
                break
            else:
                print(f"Index {current_camera_index} opened but no frame; releasing.")
                video_capture.release()
        else:
            print(f"Failed to open camera at index {current_camera_index}")
        current_camera_index += 1
    else:
        print("Could not open any camera. Exiting.")
        return

    previous_emotion_category = None

    # Open Hume streaming socket
    async with hume_client.expression_measurement.stream.connect(options=stream_options) as hume_stream:
        while True:
            ret, frame = video_capture.read()
            if not ret or frame is None:
                # Attempt camera reconnect on read failure
                print("Frame read error; reconnecting camera...")
                video_capture.release()
                video_capture = cv2.VideoCapture(current_camera_index)
                if not video_capture.isOpened():
                    print("Reconnection failed. Exiting loop.")
                    break
                continue

            try:
                # Encode frame as JPEG and then Base64 for Hume
                _, jpeg_buffer = cv2.imencode('.jpg', frame)
                if jpeg_buffer is None:
                    print("Failed to encode frame to JPEG")
                    continue
                b64_image = base64.b64encode(jpeg_buffer).decode('utf-8')

                # Send image to Hume and get predictions
                result = await hume_stream.send_file(b64_image)

                # Detect user entry/exit events
                faces_present_now = bool(result.face and result.face.predictions)
                if faces_present_now and not face_in_view_before:
                    print("User entered camera view")
                    send_message_to_arduino("UserEntered", "Center")
                elif not faces_present_now and face_in_view_before:
                    print("User left camera view")
                    send_message_to_arduino("UserLeft", "")
                face_in_view_before = faces_present_now

                # If a face is detected, compute position & emotion category
                if faces_present_now:
                    for prediction in result.face.predictions:
                        # Calculate horizontal offset from frame center
                        if prediction.bbox:
                            bbox = prediction.bbox
                            face_center_x = bbox.x + bbox.w / 2
                            frame_center_x = frame.shape[1] / 2
                            offset_px = int(face_center_x - frame_center_x)
                            if offset_px < 0:
                                position_label = f"Left {-offset_px} px"
                            elif offset_px > 0:
                                position_label = f"Right {offset_px} px"
                            else:
                                position_label = "Center 0 px"
                            print(f"[Hume] Face position: {position_label}")
                        else:
                            position_label = "Unknown"

                        # Choose the highest-scoring emotion and map to category
                        top_emotion = max(prediction.emotions, key=lambda e: e.score)
                        category = categorize_hume_emotion(top_emotion.name)
                        print(f"Detected emotion: {top_emotion.name} -> {category} ({top_emotion.score:.2f})")

                        # Only notify Arduino when category changes
                        if category != previous_emotion_category:
                            print(f"Emotion changed to: {category}")
                        arduino_resp = send_message_to_arduino(category, position_label)
                        if arduino_resp:
                            print(f"Arduino replied: {arduino_resp.decode('utf-8').strip()}")
                        previous_emotion_category = category

                # Throttle loop to avoid overloading serial/Hume
                await asyncio.sleep(2)

            except Exception as proc_err:
                print(f"Error during frame processing: {proc_err}")

    # Clean up resources
    video_capture.release()
    arduino_serial.close()


if __name__ == "__main__":
    asyncio.run(main())
    # Add your arduino port
    ARDUINO_DEFAULT_PORT = "/your/own/arduino_port"

