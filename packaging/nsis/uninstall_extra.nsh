; NSIS uninstaller extra cleanup for subcli
!macro SUBCLI_UNINSTALL_CLEANUP
  RMDir /r "$INSTDIR\data\assets"
  RMDir /r "$INSTDIR\data\state"
  RMDir /r "$INSTDIR\cache"
  RMDir /r "$INSTDIR\outputs"
  RMDir /r "$INSTDIR\logs"
  Delete "$INSTDIR\data\sub.yaml"
  Delete "$INSTDIR\config.yaml"
  RMDir "$INSTDIR\data"
  RMDir "$INSTDIR"
!macroend
