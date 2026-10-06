"""A distraction-free, one-word-at-a-time reader for .txt and .pdf files."""

from __future__ import annotations

import re
import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox
from tkinter import font as tkfont


DEFAULT_DELAY_MS = 300
STEP_MS = 50
MIN_DELAY_MS = 50


class SpeedReader:
    def __init__(self, root: tk.Tk, words: list[str], title: str) -> None:
        self.root = root
        self.words = words
        self.index = 0
        self.delay_ms = DEFAULT_DELAY_MS
        self.paused = False
        self.after_id: str | None = None

        root.title(f"Speed Reader — {title}")
        root.configure(bg="black")
        root.attributes("-fullscreen", True)
        root.bind("<space>", self.toggle_pause)
        root.bind("<Left>", self.faster)
        root.bind("<Right>", self.slower)
        root.bind("<Escape>", lambda _event: root.destroy())

        self.canvas = tk.Canvas(root, bg="black", highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)
        self.normal_font = tkfont.Font(family="Arial", size=48, weight="normal")
        self.bold_font = tkfont.Font(family="Arial", size=48, weight="bold")
        self.info = tk.Label(
            root,
            bg="black",
            fg="#a8a8a8",
            font=("Arial", 12),
            anchor="w",
        )
        self.info.place(x=20, y=18)
        self.help = tk.Label(
            root,
            text="Space: pause/resume   ←: faster   →: slower   Esc: quit",
            bg="black",
            fg="#777777",
            font=("Arial", 11),
        )
        self.help.place(relx=0.5, rely=0.97, anchor="s")
        root.bind("<Configure>", lambda _event: self.draw_word())

        self.draw_word()
        self.schedule_next()

    def draw_word(self) -> None:
        self.canvas.delete("word")
        if self.index >= len(self.words):
            self.canvas.create_text(
                self.root.winfo_width() / 2,
                self.root.winfo_height() / 2,
                text="Finished",
                fill="white",
                font=self.bold_font,
                tags="word",
            )
            self.info.configure(text=f"Finished — {len(self.words)} words")
            return

        word = self.words[self.index]
        # The character just right of center for even-length words is the focus letter.
        focus = len(word) // 2
        left, middle, right = word[:focus], word[focus], word[focus + 1 :]
        x_center = self.root.winfo_width() / 2
        y_center = self.root.winfo_height() / 2

        # Position each part so the bold focus letter's centre is the screen centre.
        left_width = self.normal_font.measure(left)
        middle_width = self.bold_font.measure(middle)
        start_x = x_center - middle_width / 2 - left_width
        self.canvas.create_text(start_x, y_center, text=left, fill="white",
                                font=self.normal_font, anchor="w", tags="word")
        self.canvas.create_text(start_x + left_width, y_center, text=middle, fill="white",
                                font=self.bold_font, anchor="w", tags="word")
        self.canvas.create_text(start_x + left_width + middle_width, y_center, text=right,
                                fill="white", font=self.normal_font, anchor="w", tags="word")
        state = "Paused" if self.paused else "Playing"
        self.info.configure(text=f"{state}  •  {self.delay_ms} ms  •  {self.index + 1} / {len(self.words)}")

    def schedule_next(self) -> None:
        if not self.paused and self.index < len(self.words):
            self.after_id = self.root.after(self.delay_ms, self.next_word)

    def next_word(self) -> None:
        self.after_id = None
        if not self.paused:
            self.index += 1
            self.draw_word()
            self.schedule_next()

    def toggle_pause(self, _event: tk.Event | None = None) -> None:
        self.paused = not self.paused
        if self.paused and self.after_id:
            self.root.after_cancel(self.after_id)
            self.after_id = None
        self.draw_word()
        if not self.paused:
            self.schedule_next()

    def faster(self, _event: tk.Event | None = None) -> None:
        self.delay_ms = max(MIN_DELAY_MS, self.delay_ms - STEP_MS)
        self.draw_word()

    def slower(self, _event: tk.Event | None = None) -> None:
        self.delay_ms += STEP_MS
        self.draw_word()


def extract_words(path: Path) -> list[str]:
    if path.suffix.lower() == ".txt":
        text = path.read_text(encoding="utf-8", errors="replace")
    else:
        try:
            from pypdf import PdfReader
        except ImportError as exc:
            raise RuntimeError("PDF support needs pypdf. Run: py -m pip install pypdf") from exc
        reader = PdfReader(path)
        text = "\n".join(page.extract_text() or "" for page in reader.pages)
    return re.findall(r"\S+", text)


def main() -> None:
    root = tk.Tk()
    root.withdraw()
    filename = filedialog.askopenfilename(
        title="Choose a text or PDF file",
        filetypes=[("Readable files", "*.txt *.pdf"), ("Text files", "*.txt"), ("PDF files", "*.pdf")],
    )
    if not filename:
        root.destroy()
        return
    try:
        path = Path(filename)
        words = extract_words(path)
        if not words:
            raise RuntimeError("No readable words were found in this file.")
    except Exception as exc:
        messagebox.showerror("Speed Reader", str(exc))
        root.destroy()
        return
    root.deiconify()
    SpeedReader(root, words, path.name)
    root.mainloop()


if __name__ == "__main__":
    main()
