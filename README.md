# Quake III DeFRaG Physics (WIP)

[![build](../../workflows/build/badge.svg)](../../actions?query=workflow%3Abuild) <a href="https://discord.com/invite/ZG4dKNVQJu"><img src="https://img.shields.io/discord/751483934034100274?color=7289da&logo=discord&logoColor=white" alt="Discord server" /></a> <a href="https://defrag.fandom.com"><img src="https://img.shields.io/static/v1?label=wiki&message=DeFRaG&color=ffc500&logo=fandom&logoColor=white" alt="DeFRaG Wiki" /></a>

This document is a continuous effort of accurately describing the physics of the Quake III mod DeFRaG and how the code dictates the way we play. It covers the math behind the CGazHUD in all sorts of scenarios and should give you a better understanding of what it displays and why. Other sections will be extended gradually.

| [Download latest pdf.](../../releases/download/master/main.pdf) |
| --------------------------------------------------------------- |

## Building
To generate the pdf, do
```
$ latexmk -pdflua -shell-escape -synctex=1 -interaction=nonstopmode main.tex
```
