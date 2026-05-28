import http.client
import urllib.parse
import re

def get_body_class(html):
    m = re.search(r'<body[^>]*class="([^"]*)"', html)
    return m.group(1) if m else ""

def get_csrf(html):
    m = re.search(r'name="csrf_token" value="([^"]*)"', html)
    return m.group(1) if m else ""

def test_flow():
    host = "localhost"
    port = 8081
    
    # 1. Initial Load
    conn = http.client.HTTPConnection(host, port)
    conn.request("GET", "/")
    r = conn.getresponse()
    html = r.read().decode()
    
    csrf_token = get_csrf(html)
    cookies_raw = r.getheader("Set-Cookie", "")
    cookies = {}
    for c in cookies_raw.split(","):
        if "=" in c:
            parts = c.split(";")[0].split("=")
            cookies[parts[0].strip()] = parts[1].strip()
    
    print(f"Step 1: Initial Load. CSRF={csrf_token}, Cookies={cookies}")
    
    if not csrf_token:
        print("FAIL: No CSRF token")
        return False

    # 2. Toggle Sidebar
    cookie_str = "; ".join([f"{k}={v}" for k, v in cookies.items()])
    headers = {
        "Content-type": "application/x-www-form-urlencoded",
        "Cookie": cookie_str
    }
    params = urllib.parse.urlencode({'csrf_token': csrf_token})
    
    conn.request("POST", "/toggle_sidebar", params, headers)
    r = conn.getresponse()
    print(f"Step 2: POST /toggle_sidebar. Status={r.status}")
    
    new_cookies_raw = r.getheader("Set-Cookie", "")
    for c in new_cookies_raw.split(","):
        if "=" in c:
            parts = c.split(";")[0].split("=")
            cookies[parts[0].strip()] = parts[1].strip()
            
    if r.status in (302, 303):
        loc = r.getheader("Location")
        cookie_str = "; ".join([f"{k}={v}" for k, v in cookies.items()])
        conn.request("GET", loc, headers={"Cookie": cookie_str})
        r = conn.getresponse()
        html = r.read().decode()
        cls = get_body_class(html)
        print(f"Step 3: Redirect to {loc}. Body class='{cls}'")
        if "sb-open" not in cls:
            print("FAIL: sb-open not in body class after toggle")
            return False
    else:
        print(f"FAIL: Expected 303, got {r.status}")
        return False

    # 3. Toggle Theme
    csrf_token = get_csrf(html) # Get fresh token just in case
    cookie_str = "; ".join([f"{k}={v}" for k, v in cookies.items()])
    params = urllib.parse.urlencode({'csrf_token': csrf_token})
    conn.request("POST", "/toggle_theme", params, headers={"Content-type": "application/x-www-form-urlencoded", "Cookie": cookie_str})
    r = conn.getresponse()
    print(f"Step 4: POST /toggle_theme. Status={r.status}")
    
    new_cookies_raw = r.getheader("Set-Cookie", "")
    for c in new_cookies_raw.split(","):
        if "=" in c:
            parts = c.split(";")[0].split("=")
            cookies[parts[0].strip()] = parts[1].strip()

    if r.status in (302, 303):
        loc = r.getheader("Location")
        cookie_str = "; ".join([f"{k}={v}" for k, v in cookies.items()])
        conn.request("GET", loc, headers={"Cookie": cookie_str})
        r = conn.getresponse()
        html = r.read().decode()
        cls = get_body_class(html)
        print(f"Step 5: Redirect to {loc}. Body class='{cls}'")
        if "theme-dark" not in cls:
            print("FAIL: theme-dark not in body class")
            return False
    else:
        print("FAIL: Expected 302")
        return False

    print("ALL STEPS PASSED")
    return True

if __name__ == "__main__":
    if not test_flow():
        exit(1)
