"""Small offline GUI for preparing expressive MIDI for arbitrary DAW instruments.

Does not synthesize sound. All inference runs in a child process, not on the UI
thread or the DAW's real-time audio thread.
"""
from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk


class ExpressiveMidiApp:
    def __init__(self, root: tk.Tk):
        self.root = root
        root.title('Expressive MIDI Bridge — Piano')
        root.minsize(650, 375)
        frame = ttk.Frame(root, padding=18)
        frame.pack(fill='both', expand=True)
        frame.columnconfigure(1, weight=1)
        self.source = tk.StringVar()
        self.target = tk.StringVar()
        self.backend = tk.StringVar(value='baseline')
        self.repo = tk.StringVar()
        self.python = tk.StringVar(value=sys.executable)
        self.bpm = tk.StringVar(value='120')
        self.strength = tk.StringVar(value='0.75')
        self.status = tk.StringVar(value='Choose a MIDI file. Baseline is not AI.')

        def row(label, variable, browse=None, index=0):
            ttk.Label(frame, text=label).grid(row=index, column=0, sticky='w', pady=4)
            ttk.Entry(frame, textvariable=variable).grid(row=index, column=1, sticky='ew', padx=8)
            if browse:
                ttk.Button(frame, text='Browse…', command=browse).grid(row=index, column=2)

        row('Input MIDI', self.source, self.pick_input, 0)
        row('Output MIDI', self.target, self.pick_output, 1)
        ttk.Label(frame, text='Backend').grid(row=2, column=0, sticky='w', pady=4)
        chooser = ttk.Combobox(frame, state='readonly', values=['baseline', 'pianist-transformer', 'identity'],
                               textvariable=self.backend)
        chooser.grid(row=2, column=1, sticky='ew', padx=8)
        ttk.Label(frame, text='DAW BPM').grid(row=3, column=0, sticky='w', pady=4)
        ttk.Entry(frame, textvariable=self.bpm, width=12).grid(row=3, column=1, sticky='w', padx=8)
        ttk.Label(frame, text='Strength (0–1)').grid(row=4, column=0, sticky='w', pady=4)
        ttk.Entry(frame, textvariable=self.strength, width=12).grid(row=4, column=1, sticky='w', padx=8)
        row('AI repo (if AI)', self.repo, self.pick_repo, 5)
        row('AI Python', self.python, self.pick_python, 6)
        ttk.Label(frame, text='AI setup: README section 3. Output is MIDI, not audio.',
                  foreground='#666').grid(row=7, column=0, columnspan=3, sticky='w', pady=(12, 3))
        self.render_button = ttk.Button(frame, text='Render expressive MIDI', command=self.render)
        self.render_button.grid(row=8, column=0, columnspan=3, sticky='ew', pady=8)
        ttk.Label(frame, textvariable=self.status, wraplength=600).grid(row=9, column=0,
                                                                         columnspan=3, sticky='w')

    def pick_input(self):
        selected = filedialog.askopenfilename(filetypes=[('MIDI', '*.mid *.midi')])
        if selected:
            self.source.set(selected)
            self.target.set(str(Path(selected).with_name(Path(selected).stem + '_expressive.mid')))

    def pick_output(self):
        selected = filedialog.asksaveasfilename(defaultextension='.mid',
                                                filetypes=[('MIDI', '*.mid')])
        if selected:
            self.target.set(selected)

    def pick_repo(self):
        selected = filedialog.askdirectory(title='Select PianistTransformer repository')
        if selected:
            self.repo.set(selected)

    def pick_python(self):
        selected = filedialog.askopenfilename(title='Select Python in AI virtual environment')
        if selected:
            self.python.set(selected)

    def render(self):
        source = Path(self.source.get()).expanduser()
        target = Path(self.target.get()).expanduser()
        if not source.is_file():
            messagebox.showerror('Input missing', 'Select an existing MIDI file.')
            return
        if not self.target.get() or source.resolve() == target.resolve():
            messagebox.showerror('Invalid output', 'Output must differ from the source.')
            return
        try:
            bpm = float(self.bpm.get())
            strength = float(self.strength.get())
            if not 20 <= bpm <= 300 or not 0 <= strength <= 1:
                raise ValueError()
        except ValueError:
            messagebox.showerror('Invalid values', 'BPM must be 20–300 and strength 0–1.')
            return
        mode = self.backend.get()
        cmd = [sys.executable, '-m', 'expressive_midi', 'render', str(source),
               str(target), '--backend', mode, '--daw-bpm', str(bpm), '--report']
        if mode == 'baseline':
            cmd += ['--strength', str(strength)]
        elif mode == 'pianist-transformer':
            if not self.repo.get() or not self.python.get():
                messagebox.showerror('AI not configured', 'Specify AI repo and AI Python executable.')
                return
            cmd += ['--repo', self.repo.get(), '--model-python', self.python.get(),
                    '--ai-strength', str(strength)]
        target.parent.mkdir(parents=True, exist_ok=True)
        self.render_button.state(['disabled'])
        self.status.set('Rendering…')

        def run():
            try:
                r = subprocess.run(cmd, text=True, capture_output=True, check=False)
                result = ('Saved: ' + str(target)) if r.returncode == 0 else ('ERROR: ' + (r.stderr or r.stdout).strip()[-1200:])
            except Exception as exc:
                result = f'ERROR: {exc}'
            def finish():
                self.render_button.state(['!disabled'])
                self.status.set(result)
            self.root.after(0, finish)
        threading.Thread(target=run, daemon=True).start()


def main():
    root = tk.Tk()
    ExpressiveMidiApp(root)
    root.mainloop()


if __name__ == '__main__':
    main()
