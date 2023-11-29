source 00_prepare.sh

cd ..
rm -rf doc/00_requirements/output-html

mkdir -p doc/00_requirements/output-html/html/postureChecker/doc/00_requirements
cp doc/01_diagrams/*.png doc/00_requirements/output-html/html/postureChecker/doc/00_requirements
cp doc/01_diagrams/*.svg doc/00_requirements/output-html/html/postureChecker/doc/00_requirements

if [ -f "scripts/plantuml.jar" ]; then
    java -jar scripts/plantuml.jar doc/00_requirements/01_diagrams/appStates.plantuml
    cp doc/01_diagrams/*.png doc/00_requirements/output-html/html/postureChecker/doc/00_requirements
else 
    echo "Please download plantuml.jar to the scripts folder"
fi

strictdoc export . --formats=html --output-dir doc/00_requirements/output-html

cd doc/00_requirements/output-html && python -m http.server
cd ../../../scripts
