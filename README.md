# xenium

[![Build Status](https://travis-ci.org/mpoeter/xenium.svg?branch=master)](https://travis-ci.org/mpoeter/xenium)

xenium is a collection of concurrent data structures and memory reclamation algorithms.
The data structures are parameterized so that they can be used with various reclamation
schemes (similar to how the STL allows customization of allocators).

This project is based on the previous work in https://github.com/mpoeter/emr

### Data Structures
At the moment the number of provided data structures is rather small since the focus so far
was on the reclamation schemes. However, the plan is to add several more data structures in
the near future.

* `michael_scott_queue` - an unbounded lock-free multi-producer/multi-consumer queue proposed by
Michael and Scott \[[9](#ref-michael-1996)\].
* `ramalhete_queue` - a fast unbounded lock-free multi-producer/multi-consumer queue proposed by
Ramalhete \[[12](#ref-ramalhete-2016)\].
* `harris_michael_list_based_set` - a lock-free container that contains a sorted set of unique objects.
This data structure is based on the solution proposed by Michael \[[6](#ref-michael-2002)\] which builds
upon the original proposal by Harris \[[4](#ref-harris-2001)\].
* `harris_michael_hash_map` - a lock-free hash-map based on the solution proposed by Michael
\[[6](#ref-michael-2002)\] which builds upon the original proposal by Harris \[[4](#ref-harris-2001)\].
* `chase_work_stealing_deque` - a work stealing deque based on the proposal by
Chase and Lev \[[2](#ref-chase-2005)\].

### Reclamation Schemes

The implementation of the reclamation schemes is based on an adapted version of the interface
proposed by Robison \[[14](#ref-robison-2013)\].

The following reclamation schemes are implemented:
* `lock_free_ref_count` \[[15](#ref-valois-1995), [8](#ref-michael-1995)\]
* `hazard_pointer` \[[7](#ref-michael-2004)\]
* `hazard_eras` \[[13](#ref-ramalhete-2017)\]
* `quiescent_state_based`
* `epoch_based` \[[3](#ref-fraser-2004)\]
* `new_epoch_based` \[[5](#ref-hart-2007)\]
* `debra` \[[1](#ref-brown-2015)\]
* `stamp_it` \[[10](#ref-pöter-2018), [11](#ref-pöter-2018-tr)\]

#### References

1. <a name="ref-brown-2015"></a>Trevor Alexander Brown.
[Reclaiming memory for lock-free data structures: There has to
be a better way](http://www.cs.utoronto.ca/~tabrown/debra/paper.podc15.pdf).
In *Proceedings of the 2015 ACM Symposium on Principles of Distributed Computing (PODC)*,
pages 261–270. ACM, 2015.

2. <a name="ref-chase-2005"></a>David Chase and Yossi Lev.
[Dynamic circular work-stealing deque](https://www.dre.vanderbilt.edu/~schmidt/PDF/work-stealing-dequeue.pdf).
In *Proceedings of the 17th Annual ACM Symposium on Parallelism in Algorithms and Architectures (SPAA)*,
pages 21–28. ACM, 2005.

3. <a name="ref-fraser-2004"></a>Keir Fraser. [*Practical lock-freedom*](https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-579.pdf).
PhD thesis, University of Cambridge Computer Laboratory, 2004.

4. <a name="ref-harris-2001"></a>Timothy L. Harris.
[A pragmatic implementation of non-blocking linked-lists](https://www.cl.cam.ac.uk/research/srg/netos/papers/2001-caslists.pdf).
In *Proceedings of the 15th International Conference on Distributed Computing (DISC)*,
pages 300–314. Springer-Verlag, 2001.

5. <a name="ref-hart-2007"></a>Thomas E. Hart, Paul E. McKenney, Angela Demke Brown, and Jonathan Walpole.
[Performance of memory reclamation for lockless synchronization](http://csng.cs.toronto.edu/publication_files/0000/0159/jpdc07.pdf).
Journal of Parallel and Distributed Computing, 67(12):1270–1285, 2007.

6. <a name="ref-michael-2002"></a>Maged M. Michael.
[High performance dynamic lock-free hash tables and list-based sets](http://www.liblfds.org/downloads/white%20papers/%5BHash%5D%20-%20%5BMichael%5D%20-%20High%20Performance%20Dynamic%20Lock-Free%20Hash%20Tables%20and%20List-Based%20Sets.pdf).
In *Proceedings of the 14th Annual ACM Symposium on Parallel Algorithms and Architectures
(SPAA)*, pages 73–82. ACM, 2002.

7. <a name="ref-michael-2004"></a>Maged M. Michael.
[Hazard pointers: Safe memory reclamation for lock-free objects](http://www.cs.otago.ac.nz/cosc440/readings/hazard-pointers.pdf).
IEEE Transactions on Parallel and Distributed Systems, 15(6):491–504, 2004.

8. <a name="ref-michael-1995"></a>Maged M. Michael and Michael L. Scott.
[Correction of a memory management method for lock-free data structures](https://pdfs.semanticscholar.org/cec0/ad7b0fc2d4d6ba45c6212d36217df1ff2bf2.pdf).
Technical report, University of Rochester, 1995.

9. <a name="ref-michael-1996"></a>Maged M. Michael and Michael L. Scott.
[Simple, fast, and practical non-blocking and blocking concurrent queue algorithms](http://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf).
In *Proceedings of the 15th Annual ACM Symposium on Principles of Distributed Computing (PODC)*,
pages 267–275. ACM, 1996.

10. <a name="ref-pöter-2018"></a>Manuel Pöter and Jesper Larsson Träff.
Brief announcement: Stamp-it, a more thread-efficient, concurrent memory reclamation scheme in the C++ memory model.
In *Proceedings of the 30th Annual ACM Symposium on Parallelism in Algorithms and Architectures (SPAA)*,
pages 355–358. ACM, 2018.

11. <a name="ref-pöter-2018-tr"></a>Manuel Pöter and Jesper Larsson Träff.
[Stamp-it, a more thread-efficient, concurrent memory reclamation scheme in the C++ memory model](https://arxiv.org/pdf/1805.08639.pdf).
Technical report, 2018.

12. <a name="ref-ramalhete-2016"></a>Pedro Ramalhete.
[FAAArrayQueue - MPMC lock-free queue (part 4 of 4)](http://concurrencyfreaks.blogspot.com/2016/11/faaarrayqueue-mpmc-lock-free-queue-part.html).
Blog, November 2016.

13. <a name="ref-ramalhete-2017"></a>Pedro Ramalhete and Andreia Correia.
[Brief announcement: Hazard eras - non-blocking memory reclamation](https://github.com/pramalhe/ConcurrencyFreaks/blob/master/papers/hazarderas-2017.pdf).
In *Proceedings of the 29th Annual ACM Symposium on Parallelism in Algorithms and Architectures (SPAA)*,
pages 367–369. ACM, 2017.

14. <a name="ref-robison-2013"></a>Arch D. Robison.
[Policy-based design for safe destruction in concurrent containers](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3712.pdf).
C++ standards committee paper, 2013.

15. <a name="ref-valois-1995"></a>John D. Valois. *Lock-Free Data Structures*. PhD thesis, Rensselaer Polytechnic Institute, 1995.