# xenium

xenium is a collection of concurrent data structures and memory reclamation algorithms.
The data structures are parameterized so that they can be used with various reclamation
schemes (similar to how the STL allows customization of allocators).

This project is based on the previous work in https://github.com/mpoeter/emr

### Data Structures
At the moment the number of provided data structures is rather small since the focus so far
was on the reclamation schemes. However, the plan is to add several more data structures in
the near future.

* `michael_scott_queue` - a lock-free multi-producer/multi-consumer queue proposed by
[Michael and Scott](http://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf).
* `ramalhete_queue` - a lock-free multi-producer/multi-consumer queue proposed by
[Ramalhete](http://concurrencyfreaks.blogspot.com/2016/11/faaarrayqueue-mpmc-lock-free-queue-part.html).
* `harris_michael_list_based_set` - a lock-free container that contains a sorted set of unique objects.
This data structure is based on the solution proposed by
[Michael](http://www.liblfds.org/downloads/white%20papers/%5BHash%5D%20-%20%5BMichael%5D%20-%20High%20Performance%20Dynamic%20Lock-Free%20Hash%20Tables%20and%20List-Based%20Sets.pdf)
which builds upon the original proposal by
[Harris](https://www.cl.cam.ac.uk/research/srg/netos/papers/2001-caslists.pdf).
* `harris_michael_hash_map` - a lock-free hash-map based on the solution proposed by
[Michael](http://www.liblfds.org/downloads/white%20papers/%5BHash%5D%20-%20%5BMichael%5D%20-%20High%20Performance%20Dynamic%20Lock-Free%20Hash%20Tables%20and%20List-Based%20Sets.pdf)
which builds upon the original proposal by [Harris](https://www.cl.cam.ac.uk/research/srg/netos/papers/2001-caslists.pdf).

### Reclamation Schemes

* Lock-Free Reference Counting 
* [Hazard Pointers](http://www.cs.otago.ac.nz/cosc440/readings/hazard-pointers.pdf)
* [Hazard Eras](https://github.com/pramalhe/ConcurrencyFreaks/blob/master/papers/hazarderas-2017.pdf)
* Quiescent State Based Reclamation
* [Epoch Based Reclamation](https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-579.pdf)
* [New Epoch Based Reclamation](http://csng.cs.toronto.edu/publication_files/0000/0159/jpdc07.pdf)
* [DEBRA](http://www.cs.utoronto.ca/~tabrown/debra/paper.podc15.pdf)
* [Stamp-it](https://arxiv.org/pdf/1805.08639.pdf)
