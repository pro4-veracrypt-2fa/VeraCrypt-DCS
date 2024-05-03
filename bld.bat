if not defined WORKSPACE (
   echo WORKSPACE not defined
   goto :eof
)
pushd %WORKSPACE%
%PYTHONHOME%\python %EDK_PREFIX%\BaseTools\Source\Python\build\build.py %*
popd
