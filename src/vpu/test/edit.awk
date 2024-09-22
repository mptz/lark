/PRINTc/ { sub(/PRINTc/,"PRN.c"); sub(/RE/,"W6"); sub(/RF/,"W7") }
/LDLc/ { sub(/LDLc/,"LDI.w"); sub(/RE/,"W6"); sub(/RF/,"W7") }
{ print }
