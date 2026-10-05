"""Малый Python: декораторы, классы, async, f-строки, match.

Докстринг на несколько строк — подсветка строки должна
держаться до закрывающих кавычек.
"""
import asyncio
from dataclasses import dataclass, field


def logged(func):
    def wrapper(*args, **kwargs):
        print(f"call {func.__name__} with {len(args)} args")  # комментарий
        return func(*args, **kwargs)
    return wrapper


@dataclass
class Point:
    x: float = 0.0
    y: float = 0.0
    tags: list[str] = field(default_factory=list)

    def distance(self, other: "Point") -> float:
        return ((self.x - other.x) ** 2 + (self.y - other.y) ** 2) ** 0.5


@logged
def classify(value):
    match value:
        case 0:
            return "zero"
        case int(n) if n < 0:
            return "negative"
        case _:
            return "positive"


async def fetch(delay: float = 0.1) -> dict:
    await asyncio.sleep(delay)
    return {"status": 200, "items": [i * i for i in range(10) if i % 2], "ok": True}


if __name__ == "__main__":
    points = [Point(1, 2), Point(3.5, -4e2)]
    nearest = min(points, key=lambda p: p.distance(Point()))
    escaped = 'tab\there and a quote \' inside'
    raw = r"C:\Projects\MyCodeApp"
    print(classify(-5), nearest, escaped, raw, None)
    asyncio.run(fetch())
