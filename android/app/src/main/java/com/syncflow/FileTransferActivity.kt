package com.syncflow

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.syncflow.databinding.ActivityFileTransferBinding
import com.syncflow.file.FileActivity
import com.syncflow.file.FileActivityAdapter
import android.view.MenuItem

class FileTransferActivity : AppCompatActivity() {

    private lateinit var binding: ActivityFileTransferBinding
    private lateinit var adapter: FileActivityAdapter
    private val handler = Handler(Looper.getMainLooper())
    
    private val statusUpdateRunnable = object : Runnable {
        override fun run() {
            refreshFileList()
            handler.postDelayed(this, 2000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityFileTransferBinding.inflate(layoutInflater)
        setContentView(binding.root)

        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        supportActionBar?.title = "File Activity"

        setupRecyclerView()
        refreshFileList()
    }

    override fun onResume() {
        super.onResume()
        handler.post(statusUpdateRunnable)
    }

    override fun onPause() {
        super.onPause()
        handler.removeCallbacks(statusUpdateRunnable)
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        return when (item.itemId) {
            android.R.id.home -> {
                onBackPressedDispatcher.onBackPressed()
                true
            }
            else -> super.onOptionsItemSelected(item)
        }
    }

    private fun setupRecyclerView() {
        adapter = FileActivityAdapter()
        binding.fileActivityRecycler.apply {
            layoutManager = LinearLayoutManager(this@FileTransferActivity)
            adapter = this@FileTransferActivity.adapter
        }
    }

    private fun refreshFileList() {
        val activities = FileActivityManager.getAllActivities()
        runOnUiThread {
            adapter.submitList(activities)
            
            val totalReceived = activities.count { it.type == "received" }
            val totalSent = activities.count { it.type == "sent" }
            val totalUpdated = activities.count { it.type == "updated" }
            
            binding.statsReceivedText.text = "Received: $totalReceived"
            binding.statsSentText.text = "Sent: $totalSent"
            binding.statsUpdatedText.text = "Updated: $totalUpdated"
        }
    }
}
