# --- run as package module and as a script ---
if __name__ == "__main__" and __package__ is None:
    import os, sys
    sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
    __package__ = "app"
# ------------------------------------------------------------

from app.session import tcp_session

def main():
    tcp_session()

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n[info] Session interrupted by user.")
    finally:
        print("[info] Bye.")
