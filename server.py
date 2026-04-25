#!/usr/bin/env python3
import zlib
import struct
import socket
import sys

# ==================== CONFIGURABLE SETTINGS ====================
# OTA Header Settings
OTA_MAGIC = 0xABCDEFAB      
OTA_VERSION = 0x04      

# File Settings
APP_BIN = "application.bin" 
OTA_IMAGE = "ota_image.bin" 

# Server Settings
SERVER_IP = "0.0.0.0"  
SERVER_PORT = 5678      
CHUNK_SIZE = 512      

# Protocol Commands
START_CMD = b"GET firmware\n"      
READY_CMD = b"A"        

# Behavior Settings
SAVE_OTA_FILE = True        
AUTO_START_SERVER = True  
# ================================================================

def create_ota_image(app_file=APP_BIN, output_file=OTA_IMAGE, save_to_disk=SAVE_OTA_FILE):
    """Create OTA image from application binary"""
    try:
        with open(app_file, "rb") as f:
            app = f.read()
    except FileNotFoundError:
        print(f"Error: {app_file} not found!")
        return None
    
    crc = zlib.crc32(app) & 0xFFFFFFFF
    size = len(app)
    
    # Use configurable magic and version
    hdr = struct.pack(
        "<IIII",
        OTA_MAGIC,    # magic number
        size,
        crc,
        OTA_VERSION   # version
    )
    
    firmware = hdr + app
    
    if save_to_disk:
        with open(output_file, "wb") as f:
            f.write(firmware)
        print(f"OTA image saved to: {output_file}")
    
    print(f"OTA image created:")
    print(f"  Magic: 0x{OTA_MAGIC:08X}")
    print(f"  Version: 0x{OTA_VERSION:02X}")
    print(f"  Application size: {size} bytes")
    print(f"  Total OTA size: {len(firmware)} bytes")
    print(f"  CRC32: 0x{crc:08X}")
    
    return firmware

def run_ota_server(firmware_data):
    """Run OTA server to send firmware"""
    if firmware_data is None:
        print("Error: No firmware data to send!")
        return False
    
    file_size = len(firmware_data)
    print(f"\nFirmware size: {file_size} bytes")
    
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((SERVER_IP, SERVER_PORT))
    srv.listen(1)
    
    print(f"OTA Server listening on {SERVER_IP}:{SERVER_PORT}...")
    conn, addr = srv.accept()
    print(f"Client connected from {addr}")
    
    # Wait for START command
    print("Waiting for START command from client...")
    try:
        cmd = conn.recv(64)
    except socket.timeout:
        print("Timeout waiting for START command")
        conn.close()
        srv.close()
        return False
    
    if cmd.strip() != START_CMD.strip():
        print(f"Unexpected command: {cmd}")
        conn.close()
        srv.close()
        return False
    
    print("START received. Sending firmware...")
    
    sent = 0
    chunk_id = 0
    
    # Send first chunk
    chunk = firmware_data[sent:sent + CHUNK_SIZE]
    conn.sendall(chunk)
    print(f"[TX] Chunk {chunk_id}, {len(chunk)} bytes")
    sent += len(chunk)
    chunk_id += 1
    
    # Send remaining chunks based on client ready signals
    while sent < file_size:
        try:
            ready = conn.recv(64)
        except (ConnectionResetError, socket.error):
            print("Client disconnected unexpectedly")
            break
        
        if not ready or ready.strip() != READY_CMD.strip():
            print("Client not ready or disconnected. Stopping transfer.")
            break
        
        chunk = firmware_data[sent:sent + CHUNK_SIZE]
        conn.sendall(chunk)
        print(f"[TX] Chunk {chunk_id}, {len(chunk)} bytes")
        
        sent += len(chunk)
        chunk_id += 1
    
    print(f"\nOTA transfer finished! Sent {sent} bytes in {chunk_id} chunks")
    
    conn.close()
    srv.close()
    return True

def main():
    print("=== OTA Image Generator & Server ===\n")
    print(f"Configuration:")
    print(f"  Magic: 0x{OTA_MAGIC:08X}")
    print(f"  Version: 0x{OTA_VERSION:02X}")
    print(f"  Server: {SERVER_IP}:{SERVER_PORT}")
    print(f"  Chunk size: {CHUNK_SIZE} bytes")
    print()
    
    # Step 1: Create OTA image (in memory, optionally to disk)
    print("Step 1: Creating OTA image...")
    firmware = create_ota_image()
    
    if firmware is None:
        print("Failed to create OTA image. Exiting.")
        return
    
    # Step 2: Start server based on configuration
    print(f"\nStep 2: Starting OTA server")
    
    if AUTO_START_SERVER:
        print("Auto-starting server...")
        run_ota_server(firmware)
    else:
        response = input("Start OTA server? (y/n): ").strip().lower()
        if response == 'y':
            run_ota_server(firmware)
        else:
            print("Server not started. OTA image is ready for later use.")

if __name__ == "__main__":
    main()