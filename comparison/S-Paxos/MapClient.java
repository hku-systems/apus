package lsr.paxos.test;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.ByteBuffer;
import java.util.Random;

import lsr.paxos.ReplicationException;
import lsr.paxos.client.Client;

public class MapClient {
    private Client client;
    private long timestart;
    static long total = 100;
    static long client_num = 1;
    Random rnd = new Random(1212);
    public long getRandom() {
        return rnd.nextLong() % 10000;
    }
    public void run() throws IOException, ReplicationException {
        client = new Client();
        client.connect();
	timestart = 0;
	long timeend = 0;
        int i = 0;
        Long key, value;
	long total_latency = 0;
	long request_count = 0;
	long batch_start = 0;
	long batch_latency = 0;
	int j = 0;
        while (i < total) {
            String line;
            key = (long)i;
            value = (long)i;
	    request_count++;
	    i++;
	    timestart = System.nanoTime();
            MapServiceCommand command = new MapServiceCommand(key, value);
            byte[] response = client.execute(command.toByteArray());
	    timeend = System.nanoTime();
	    total_latency += (timeend - timestart)/1000;
	    if ((timeend - batch_start) >= 2000000000){
		batch_latency = (timeend - batch_start)/10000000;
		System.out.println((total_latency/request_count) + " " + (request_count*100/batch_latency) + " " + (j++));
		batch_start = System.nanoTime();
		request_count = 0;
		total_latency = 0;
	    }
//            ByteBuffer buffer = ByteBuffer.wrap(response);
//            Long previousValue = buffer.getLong();
//            System.out.println(String.format("Previous value for %d was %d", key, previousValue));
        }
    }

    private static void instructions() {
        System.out.println("Provide key-value pair of integers to insert to hash map");
        System.out.println("<key> <value>");
    }

    public static void main(String[] args) throws IOException, ReplicationException {
//        instructions();
        MapClient client = new MapClient();
        total = Long.parseLong(args[0]);
	client_num = Long.parseLong(args[1]);
	for (int i = 0;i < client_num - 1;i++){
		new Thread()
		{
		public void run() {
			MapClient thisClient = new MapClient();
			try{
			thisClient.run();
			} catch (Exception e){

			}
		}
		}.start();
		
	}
        client.run();
    }
}
