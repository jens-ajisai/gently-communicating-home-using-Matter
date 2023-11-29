if [ -f "plantuml.jar" ]; then
    java -jar plantuml.jar sequenceFeedbacklight.plantuml
else 
    echo "Please download plantuml.jar to the scripts folder"
fi
