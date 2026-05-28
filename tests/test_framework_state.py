#!/usr/bin/env python3
import requests
import sys
import subprocess
import time
import os

PORT = 8085
BASE_URL = f"http://127.0.0.1:{PORT}"

def run_tests():
    # 1. Build and Start the Framework's own dev server
    print("Building framework test server...")
    subprocess.run(["python3", "cmake/gen_states.py", "."], check=True)
    subprocess.run(["python3", "cmake/gen_routes.py", "."], check=True)
    subprocess.run(["cmake", "-B", "build", "-S", "."], check=True)
    subprocess.run(["cmake", "--build", "build"], check=True)
    
    server = subprocess.Popen(["./build/revet", str(PORT)])
    time.sleep(1) # Wait for start
    
    try:
        print("Testing State persistence...")
        s = requests.Session()
        
        # Initial check
        r = s.get(f"{BASE_URL}/test_state")
        if r.text != "NO_STATE":
            print(f"FAILED: Expected NO_STATE, got {r.text}")
            sys.exit(1)
            
        # Set state via POST
        r = s.post(f"{BASE_URL}/test_state", data={"val": "FrameworkMagic"}, allow_redirects=True)
        if r.text != "STATE:FrameworkMagic":
            print(f"FAILED: Expected STATE:FrameworkMagic, got {r.text}")
            sys.exit(1)
            
        # Verify persistence on second GET
        r = s.get(f"{BASE_URL}/test_state")
        if r.text != "STATE:FrameworkMagic":
            print(f"FAILED: State lost on second request! Got {r.text}")
            sys.exit(1)
            
        print("✔ Framework State tests passed!")
        
    finally:
        server.terminate()
        server.wait()

if __name__ == "__main__":
    run_tests()
