print("T1")
import sys
print("T2")
try:
    import asyncio
    print("T3: asyncio imported")
except Exception as e:
    print("T3: asyncio FAILED:", e)
    sys.print_exception(e)

print("T4")

def read_file(path):
    try:
        f = open(path)
        data = f.read()
        f.close()
        return data
    except:
        return None

src = read_file('/code.py')
if src:
    print("T5: executing code.py")
    try:
        exec(compile(src, 'code.py', 'exec'), {'__name__': '__main__'})
    except Exception as e:
        print("T5: error:", e)
    print("T6: code.py done")
else:
    print("T5: no code.py")

print("T7: all done")
