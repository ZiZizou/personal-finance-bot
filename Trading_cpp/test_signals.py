from telegram_listener import analyze_signals_status, send_message
import os

print("Testing analyze_signals_status() and send_message()...")
try:
    status = analyze_signals_status()
    print(f"Status generated (length: {len(status)})")
    
    print("Sending message to Telegram...")
    send_message("<b>[TEST]</b> Verifying /signals response functionality...")
    send_message(status)
    print("Messages sent. Please check your Telegram.")
except Exception as e:
    print(f"CRASHED: {e}")
    import traceback
    traceback.print_exc()
