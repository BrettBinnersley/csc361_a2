#
#MakeFile
#
#Brett Binnersley, V00776751
#Csc 361, Assignment #2 programming part
#


all: makeReceiver makeSender

clean:
	cleanRdpr
	cleanRdps



makeReceiver:
	g++ RDPReceiver.cpp Shared.cpp -o rdpr
makeSender:
	g++ RDPSender.cpp Shared.cpp -o rdps




cleanRdpr:
	rm rdpr

cleanRdps:
	rm rdps




