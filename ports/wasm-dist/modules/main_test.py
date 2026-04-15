print("T1: before imports")
import sys
import asyncio
import time
print("T2: imports done")

async def _test():
    print("T3: async coroutine running")
    await asyncio.sleep(0)
    print("T4: after await")

print("T5: calling asyncio.run")
asyncio.run(_test())
print("T6: asyncio.run returned")
