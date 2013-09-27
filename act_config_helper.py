#!/usr/bin/python
####
# Act config creator to help configuration for tests
####

import cmd
import os

class RunCommand(cmd.Cmd):
	__VERSION__ = "0.0.1"
	prompt = "ACT> "
	name = "Aerospike ACT config creator"


	def do_EOF(self, line):
		return True
	def do_exit(self, line):
		return True

	def do_createconfig(self,line):
	
		device_names = ''
		queue_per_device = 'no'
		num_queues = 8
		threads_per_queue = 8
		test_duration_sec = 86400
		report_interval_sec = 1
		read_req_num_512_blocks = 3
		large_block_op_kbytes = 128
		use_valloc= 'no'
		num_write_buffers = 256
		scheduler_mode = 'noop'
		use_standard = True
		
		try:
			response = ''
			### Ask for details ###
			
			while (not response.isdigit()):
				response = raw_input('Enter the number of devices you want to create config for: ')
				if response == '':
					continue
			
			no_of_devices = int(response)
			
			device_list = ''
			
			print 'Enter either raw device if over-provisioned using hdparm or partition if over-provisioned using fdisk'
			
			for device in range(1,no_of_devices + 1):
				response = ''
				while response == '':
					response = raw_input('Enter device name # '+str(device)+'(e.g. /dev/sdb or /dev/sdb1): ')
					if not response == '':
						device_list += response
					if device != no_of_devices:
						device_list += ','
			
			device_names = device_list

			print 'Duration for the test (default :'+str(test_duration_sec/3600) +' hours)'	
			td = raw_input('Configure test duration ? (N for using default) (y/N) :')
			if 'y' == td or 'Y' == td:
				response = 'x'
				while (not response.isdigit()):
					response = raw_input('Enter the test duration in hours: ')
					if response == '':
						continue
				tds = int(response)
				test_duration_sec = tds * 3600
			
			response = raw_input('Use advanced mode for configuration ? (y/N) ')
			if 'y' == response or 'Y' == response:
				
				rwops= raw_input('Configure read/writes ops per sec(y/N) :')
				
				if 'y' == rwops or 'Y' == rwops:
					response = 'x'	
					while (not response.isdigit()):
						response = raw_input('Enter Read ops per second for each device: ')
						if response == '':
							continue
					read_reqs_per_sec = response * no_of_devices
					
					response = 'x'	
					while (not response.isdigit()):
						response = raw_input('Enter write ops per second for each device: ')
						if response == '':
							continue

					large_block_ops_per_sec = int(round((no_of_devices * response * 23.5) /1000))
					
					if large_block_ops_per_sec and read_reqs_per_sec:
						use_standard = False
				
				numq= raw_input('Change num-queues default :'+str(num_queues) +' ? (y/N) :')
				if 'y' == numq or 'Y' == numq:
					response = 'x'
					while (not response.isdigit()):
						response = raw_input('Enter num-queues value: ')
						if response == '':
							continue
					num_queues = response
					
				tpq= raw_input('Change threads-per-queue default :'+str(threads_per_queue) +' ? (y/N) :')
				if 'y' == tpq or 'Y' == tpq:
					response = 'x'
					while (not response.isdigit()):
						response = raw_input('Enter threads_per_queue value: ')
						if response == '':
							continue
					threads_per_queue = response
									
				
			if use_standard:
				response = 'x'	
				while (not response.isdigit()):
					print '"1x" load is  2000 reads per sec and 1000 writes per sec'
					response = raw_input('Enter the load you want to test the devices ( e.g. enter 1 for 1x test):')
					if response == '':
						continue

				device_load = int(response)
				read_reqs_per_sec = no_of_devices * device_load * 2000
				large_block_ops_per_sec = int(round(no_of_devices * device_load * 23.5))
													
			response = ''
			while response == '':
				response = raw_input('Do you want to Create the config (Save to a file) ? : (y/N) ')
				if 'y' == response or 'Y' == response:
					actfile= 'actconfig_'+str(device_load)+ 'x_'+str(no_of_devices)+'d.txt'
					try:
						act_file_fd = open(actfile, "wb")
						act_file_fd.write('########## \n')
						act_file_fd.write('act config file for testing '+ str(no_of_devices)+' device at '+ str(device_load) +'x load \n')
						act_file_fd.write('########## \n\n')
						act_file_fd.write('# comma-separated list \n')
						act_file_fd.write('device-names: %s \n\n' % str(device_names))
						act_file_fd.write('# yes|no - default is no: \n') 
						act_file_fd.write('queue-per-device: %s \n\n' % str(queue_per_device))
						act_file_fd.write('# mandatory non-zero, ignored if queue-per-device is yes: \n')
						act_file_fd.write('num-queues: %s \n\n' % str(num_queues))
						act_file_fd.write('# mandatory non-zero:\n')
						act_file_fd.write('threads-per-queue: %s \n\n' % str(threads_per_queue))
						act_file_fd.write('test-duration-sec:  %s \n' % str(test_duration_sec))
						act_file_fd.write('report-interval-sec:  %s \n' % str(report_interval_sec))
						act_file_fd.write('read-reqs-per-sec: %s \n' % str(read_reqs_per_sec))
						act_file_fd.write('large-block-ops-per-sec: %s \n' % str(large_block_ops_per_sec))
						act_file_fd.write('read-req-num-512-blocks: %s \n' % str(read_req_num_512_blocks))
						act_file_fd.write('large-block-op-kbytes: %s \n\n' % str(large_block_op_kbytes))
						act_file_fd.write('# yes|no - default is no:\n')
						act_file_fd.write('use-valloc: %s \n\n' % str(use_valloc))
						act_file_fd.write('# if 0, will write all zeros every time:\n')
						act_file_fd.write('num-write-buffers: %s \n\n' % str(num_write_buffers))
						act_file_fd.write('# noop|cfq - default is noop \n')
						act_file_fd.write('scheduler-mode: %s \n' % str(scheduler_mode))
						act_file_fd.close()
						print 'Config File '+ str(actfile) + ' successfully created'
						
					except Exception, i:
						print "Exception : ",i
		except Exception, i:
			print '\n Got Exception.',i

if __name__ == '__main__':
	try:
		RunCommand().onecmd('createconfig')
	except (KeyboardInterrupt, SystemExit):
		print "getting error"
