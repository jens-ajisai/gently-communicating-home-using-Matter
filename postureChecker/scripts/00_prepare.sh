if virtualenv --version ; then
    echo "virtualenv is installed"
else
    pip install virtualenv
fi

if [ ! -d "ENV" ] 
then
    virtualenv ENV
    source ENV/bin/activate
    pip install -r requirements.txt
else
    source ENV/bin/activate
fi

if [ ! -d "../ext" ]
then
    cd ..
    mkdir ext
    cd ext
    git clone https://github.com/ceva-dsp/sh2.git
    cd sh2/
    git checkout tags/v1.3.0
fi

source 00_config_credentials.sh