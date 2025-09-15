# Simple test of digitalio functionality

print("=== Testing digitalio ===")

try:
    import digitalio
    print("✅ digitalio imported successfully")
    
    # Test enum values
    print(f"Direction.INPUT = {digitalio.Direction.INPUT}")
    print(f"Direction.OUTPUT = {digitalio.Direction.OUTPUT}")
    print(f"Pull.NONE = {digitalio.Pull.NONE}")
    print(f"Pull.UP = {digitalio.Pull.UP}")
    print(f"Pull.DOWN = {digitalio.Pull.DOWN}")
    
except ImportError as e:
    print(f"❌ Failed to import digitalio: {e}")
except Exception as e:
    print(f"❌ Error: {e}")

print("\n=== Testing board module ===")
try:
    import board
    print("✅ board imported successfully")
    print(f"Board ID: {board.board_id}")
    print(f"Available items: {dir(board)}")
    
except Exception as e:
    print(f"❌ Error with board: {e}")

print("\n=== Test Complete ===")