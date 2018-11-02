#!/usr/bin/python

import socket, time, json, time, random, sys
from time import sleep

loop = False
UDP_IP = "127.0.0.1"
# UDP_IP = "255.255.255.255"
# UDP_IP = "192.168.100.255"
# UDP_IP = "192.168.30.255" # 169.254.255.255

def main(port, rate, data, loop):
	delay = 1000./float(rate)
	try:
		print "creating socket on ", UDP_IP, " port ", port
		sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) # UDP   
		sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

		with open(data, "r") as f:
			print "loading data from ", data, "..."
			allLines = f.readlines()
			run = 1
			progress = 0
			print "start reading data..."
			while run > 0:
				for line in allLines:
					try:
						jsonPacket = json.loads(line)
						payload = bytes(json.dumps(jsonPacket).encode('utf-8'))
						sock.sendto(payload, (UDP_IP, port))
						progress += 1
						pct = round(float(progress)/float(len(allLines))*10000.)/100
						print "SENT ", progress, "/", len(allLines), " (" , pct, "%) : ", payload
						sleep(delay/1000.)
					except ValueError as e:
						# print "error parsing json", e, line
						pass
				if not loop:
					run -= 1
		print "completed."
	except Exception as e:
		print "caught exception: ", e
		exit(1)

nArgs = 3

if __name__ == '__main__':
	if len(sys.argv) <= nArgs:
		print ("specify port, rate, data file")
		exit(1)

	port = int(sys.argv[1])
	rate = float(sys.argv[2])
	data = sys.argv[3]

	print "sending data from ", data, " on port ", port, " at rate ", rate, " looping: ", loop
	main(port, rate, data, loop)