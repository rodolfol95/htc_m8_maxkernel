

typedef ssize_t (*io_fn_t)(struct file *, char __user *, size_t, loff_t *);
typedef ssize_t (*iov_fn_t)(struct kiocb *, const struct iovec *,
		unsigned long, loff_t);

ssize_t do_aio_read(struct kiocb *kiocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t pos);
ssize_t do_aio_write(struct kiocb *kiocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t pos);
ssize_t do_sync_readv_writev(struct file *filp, const struct iovec *iov,
		unsigned long nr_segs, size_t len, loff_t *ppos, iov_fn_t fn);
ssize_t do_loop_readv_writev(struct file *filp, struct iovec *iov,
		unsigned long nr_segs, loff_t *ppos, io_fn_t fn);
