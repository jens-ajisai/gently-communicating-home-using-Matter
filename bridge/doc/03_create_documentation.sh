if [ -f "plantuml.jar" ]; then
java -jar plantuml.jar sequenceBridge.plantuml
java -jar plantuml.jar sequenceOverall.plantuml
else 
    echo "Please download plantuml.jar to the scripts folder"
fi

