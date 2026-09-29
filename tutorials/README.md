# Built-in Mini Tutorials

Mini Tutorials are short, focused guides to a single editing task. Keep each
guide concise so users can follow it while working in the editor.

Mini Tutorials are HTML pages compiled into the application through
[`tutorials.qrc`](tutorials.qrc). Keep each mini tutorial in its own directory with
an `index.html` page and an optional `images/` subdirectory. Use relative image
paths in the HTML, such as `<img src="images/door-step-1.png">`, and add each
page and image to the `tutorials.qrc` file with a matching alias under its
mini tutorial directory. The Help → Mini Tutorials menu is declared in
`src/mainwindow.cpp`; add one action there for each mini tutorial page.
